<?php

require_once '../config/database.php';
require_once '../includes/response.php';


// =====================================================
// VALIDAR MÉTODO
// =====================================================

if ($_SERVER['REQUEST_METHOD'] !== 'POST') {

    response(
        false,
        'Método no permitido',
        null,
        405
    );
}


// =====================================================
// LEER JSON
// =====================================================

$input = json_decode(
    file_get_contents('php://input'),
    true
);

$dispositivo_id = $input['dispositivo_id'] ?? null;
$uid = $input['uid'] ?? null;


// =====================================================
// VALIDAR PARÁMETROS
// =====================================================

if (!$dispositivo_id || !$uid) {

    response(
        false,
        'Faltan parámetros',
        null,
        400
    );
}


// =====================================================
// NORMALIZAR UID
// =====================================================

$uid = strtoupper(trim($uid));


// =====================================================
// OBTENER CONFIGURACIÓN DEL DISPOSITIVO
// =====================================================

$sql = "
    SELECT
        tiempo_apertura,
        intentos_maximos,
        tiempo_bloqueo,
        rfid_habilitado
    FROM configuracion
    WHERE dispositivo_id = ?
    LIMIT 1
";

$stmt = $pdo->prepare($sql);

$stmt->execute([
    $dispositivo_id
]);

$configuracion = $stmt->fetch();


// =====================================================
// VERIFICAR QUE EXISTA CONFIGURACIÓN
// =====================================================

if (!$configuracion) {

    response(
        false,
        'No existe configuración para el dispositivo',
        null,
        500
    );
}


// =====================================================
// GUARDAR CONFIGURACIÓN
// =====================================================

$tiempo_apertura = (int)$configuracion['tiempo_apertura'];
$intentos_maximos = (int)$configuracion['intentos_maximos'];
$tiempo_bloqueo = (int)$configuracion['tiempo_bloqueo'];
$rfid_habilitado = (int)$configuracion['rfid_habilitado'];


// =====================================================
// VERIFICAR SI RFID ESTÁ HABILITADO
// =====================================================

if ($rfid_habilitado == 0) {

    response(
        true,
        'RFID deshabilitado',
        [
            'acceso' => false,
            'metodo' => 'rfid',
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}


// =====================================================
// BUSCAR LA TARJETA RFID
// =====================================================

$sql = "
    SELECT
        t.id AS tarjeta_id,
        t.usuario_id,
        u.nombre,
        u.apellido
    FROM tarjetas_rfid t

    INNER JOIN usuarios u
        ON u.id = t.usuario_id

    WHERE t.uid = ?
      AND t.dispositivo_id = ?
      AND t.estado = 'activa'
      AND u.estado = 'activo'

    LIMIT 1
";

$stmt = $pdo->prepare($sql);

$stmt->execute([
    $uid,
    $dispositivo_id
]);

$usuario = $stmt->fetch();


// =====================================================
// TARJETA AUTORIZADA
// =====================================================

if ($usuario) {

    // -----------------------------------------------
    // Registrar acceso permitido
    // -----------------------------------------------

    $sql = "
        INSERT INTO registros_acceso
        (
            dispositivo_id,
            usuario_id,
            metodo,
            resultado,
            identificador
        )
        VALUES
        (?, ?, 'rfid', 'permitido', ?)
    ";

    $stmt = $pdo->prepare($sql);

    $stmt->execute([
        $dispositivo_id,
        $usuario['usuario_id'],
        $uid
    ]);


    // -----------------------------------------------
    // Respuesta al ESP32
    // -----------------------------------------------

    response(
        true,
        'Acceso autorizado',
        [
            'acceso' => true,
            'usuario_id' => $usuario['usuario_id'],
            'nombre' => $usuario['nombre'],
            'apellido' => $usuario['apellido'],
            'metodo' => 'rfid',

            // Configuración
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}


// =====================================================
// TARJETA NO AUTORIZADA
// =====================================================

else {

    // -----------------------------------------------
    // Registrar acceso denegado
    // -----------------------------------------------

    $sql = "
        INSERT INTO registros_acceso
        (
            dispositivo_id,
            metodo,
            resultado,
            identificador
        )
        VALUES
        (?, 'rfid', 'denegado', ?)
    ";

    $stmt = $pdo->prepare($sql);

    $stmt->execute([
        $dispositivo_id,
        $uid
    ]);


    // -----------------------------------------------
    // Respuesta al ESP32
    // -----------------------------------------------

    response(
        true,
        'Acceso denegado',
        [
            'acceso' => false,
            'metodo' => 'rfid',

            // Configuración
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}