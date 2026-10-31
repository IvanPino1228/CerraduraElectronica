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
$fingerprint_id = $input['fingerprint_id'] ?? null;


// =====================================================
// VALIDAR PARÁMETROS
// =====================================================

if (!$dispositivo_id || $fingerprint_id === null) {

    response(
        false,
        'Faltan parámetros',
        null,
        400
    );
}


// =====================================================
// OBTENER CONFIGURACIÓN DEL DISPOSITIVO
// =====================================================

$sql = "
    SELECT
        tiempo_apertura,
        intentos_maximos,
        tiempo_bloqueo,
        huella_habilitada
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
$huella_habilitada = (int)$configuracion['huella_habilitada'];


// =====================================================
// VERIFICAR SI LA HUELLA ESTÁ HABILITADA
// =====================================================

if ($huella_habilitada == 0) {

    response(
        true,
        'Huella deshabilitada',
        [
            'acceso' => false,
            'metodo' => 'huella',
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}


// =====================================================
// BUSCAR LA HUELLA
// =====================================================

$sql = "
    SELECT
        h.id AS huella_id,
        h.usuario_id,
        u.nombre,
        u.apellido
    FROM huellas h

    INNER JOIN usuarios u
        ON u.id = h.usuario_id

    WHERE h.fingerprint_id = ?
      AND h.dispositivo_id = ?
      AND h.estado = 'activa'
      AND u.estado = 'activo'

    LIMIT 1
";

$stmt = $pdo->prepare($sql);

$stmt->execute([
    $fingerprint_id,
    $dispositivo_id
]);

$usuario = $stmt->fetch();


// =====================================================
// HUELLA AUTORIZADA
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
        (?, ?, 'huella', 'permitido', ?)
    ";

    $stmt = $pdo->prepare($sql);

    $stmt->execute([
        $dispositivo_id,
        $usuario['usuario_id'],
        $fingerprint_id
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
            'metodo' => 'huella',

            // Configuración
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}


// =====================================================
// HUELLA NO AUTORIZADA
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
        (?, 'huella', 'denegado', ?)
    ";

    $stmt = $pdo->prepare($sql);

    $stmt->execute([
        $dispositivo_id,
        $fingerprint_id
    ]);


    // -----------------------------------------------
    // Respuesta al ESP32
    // -----------------------------------------------

    response(
        true,
        'Acceso denegado',
        [
            'acceso' => false,
            'metodo' => 'huella',

            // Configuración
            'tiempo_apertura' => $tiempo_apertura,
            'intentos_maximos' => $intentos_maximos,
            'tiempo_bloqueo' => $tiempo_bloqueo
        ]
    );
}