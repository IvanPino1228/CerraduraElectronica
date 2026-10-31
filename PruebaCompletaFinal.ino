#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <Adafruit_Fingerprint.h>
#include <SPI.h>
#include <MFRC522.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ============================================================
// WIFI
// ============================================================

const char* ssid = "iPhone de Mary";
const char* password = "123456788";

// ============================================================
// API
// ============================================================

const char* apiHuella =
  "http://172.20.10.2/cerradura-api/api/huella.php";

const char* apiRFID =
  "http://172.20.10.2/cerradura-api/api/rfid.php";
  
const int dispositivoID = 1;

// ============================================================
// RELÉ
// ============================================================

const int RELAY_PIN = 25;

#define RELAY_ON  LOW
#define RELAY_OFF HIGH

// ============================================================
// SENSOR DE HUELLA
// ============================================================

HardwareSerial FingerSerial(2);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&FingerSerial);

// ESP32
// RX2 = GPIO16
// TX2 = GPIO17

// ============================================================
// RFID RC522
// ============================================================

#define SS_PIN  5
#define RST_PIN 21

MFRC522 rfid(SS_PIN, RST_PIN);

// ============================================================
// PANTALLA LCD I2C
// ============================================================

// LCD 16x2 con modulo I2C.
// Se usan GPIO26 (SDA) y GPIO27 (SCL) para no interferir
// con el RC522, que utiliza GPIO21 como RST.
#define LCD_SDA 26
#define LCD_SCL 27
#define LCD_ADDRESS 0x27

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

String lcdLinea1Actual = "";
String lcdLinea2Actual = "";

// SPI
// SCK  = GPIO18
// MISO = GPIO19
// MOSI = GPIO23
// SS   = GPIO5

// ============================================================
// SEGURIDAD
// ============================================================

// Número de intentos fallidos actuales
int intentosFallidos = 0;

// Máximo de intentos permitido.
// Este valor será actualizado desde la API.
int intentosMaximos = 0;

// Tiempo de bloqueo en segundos.
// Este valor será actualizado desde la API.
int tiempoBloqueo = 0;

// Estado del sistema
bool sistemaBloqueado = false;

// Momento en que comenzó el bloqueo
unsigned long inicioBloqueo = 0;

// Evita procesar repetidamente la misma huella mientras el dedo
// permanece sobre el sensor.
bool esperandoRetiroHuella = false;

// Pequeño tiempo de estabilizacion para evitar lecturas instantaneas
// o ruido del sensor.
const unsigned long ESPERA_ESTABILIZACION_HUELLA = 150;

// ============================================================
// FUNCIONES LCD
// ============================================================

String ajustarLineaLCD(String texto) {

  if (texto.length() > 16) {
    return texto.substring(0, 16);
  }

  while (texto.length() < 16) {
    texto += " ";
  }

  return texto;
}

void mostrarLCD(String linea1, String linea2) {

  linea1 = ajustarLineaLCD(linea1);
  linea2 = ajustarLineaLCD(linea2);

  // Evita borrar y reescribir la pantalla si el mensaje no cambió.
  if (linea1 == lcdLinea1Actual && linea2 == lcdLinea2Actual) {
    return;
  }

  lcdLinea1Actual = linea1;
  lcdLinea2Actual = linea2;

  lcd.setCursor(0, 0);
  lcd.print(linea1);

  lcd.setCursor(0, 1);
  lcd.print(linea2);
}

void mostrarLCDSistemaListo() {
  mostrarLCD("SISTEMA LISTO", "Huella / RFID");
}

// ============================================================
// SETUP
// ============================================================

void setup() {

  Serial.begin(115200);

  // ----------------------------------------------------------
  // LCD I2C
  // ----------------------------------------------------------

  Wire.begin(LCD_SDA, LCD_SCL);
  lcd.init();
  lcd.backlight();
  mostrarLCD("INICIANDO...", "Cerradura ESP32");

  // ----------------------------------------------------------
  // RELÉ
  // ----------------------------------------------------------

  pinMode(RELAY_PIN, OUTPUT);
  digitalWrite(RELAY_PIN, RELAY_OFF);

  Serial.println();
  Serial.println("====================================");
  Serial.println("   SISTEMA DE CERRADURA ESP32");
  Serial.println("====================================");

  // ----------------------------------------------------------
  // WIFI
  // ----------------------------------------------------------

  Serial.println("Conectando a WiFi...");
  mostrarLCD("CONECTANDO WIFI", "Espere...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {

    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");
  mostrarLCD("WIFI CONECTADO", "Iniciando...");
  Serial.print("IP ESP32: ");
  Serial.println(WiFi.localIP());

  // ----------------------------------------------------------
  // HUELLA
  // ----------------------------------------------------------

  FingerSerial.begin(
    57600,
    SERIAL_8N1,
    16,
    17
  );

  finger.begin(57600);

  Serial.println("Verificando sensor de huella...");

  if (!finger.verifyPassword()) {

    Serial.println("ERROR: sensor de huella no encontrado");
    mostrarLCD("ERROR HUELLA", "Revise sensor");

    while (true) {
      delay(1000);
    }
  }

  Serial.println("Sensor de huella OK");
  mostrarLCD("HUELLA OK", "Iniciando RFID");

  // ----------------------------------------------------------
  // RFID
  // ----------------------------------------------------------

  SPI.begin(
    18,   // SCK
    19,   // MISO
    23,   // MOSI
    5     // SS
  );

  rfid.PCD_Init();

  Serial.println("RC522 OK");
  mostrarLCDSistemaListo();

  Serial.println();
  Serial.println("Sistema listo");
  Serial.println();
}

// ============================================================
// LOOP
// ============================================================

void loop() {

  // ==========================================================
  // 1. COMPROBAR SI EL SISTEMA ESTÁ BLOQUEADO
  // ==========================================================

  if (sistemaBloqueado) {

    mostrarLCD("SISTEMA BLOQUEADO", "Espere...");
    comprobarBloqueo();

    // Mientras esté bloqueado no procesamos
    // RFID ni huella.
    delay(100);

    return;
  }

  // ==========================================================
  // 2. RFID
  // ==========================================================

  if (
    rfid.PICC_IsNewCardPresent() &&
    rfid.PICC_ReadCardSerial()
  ) {

    mostrarLCD("RFID DETECTADO", "Verificando...");
    leerRFID();

    rfid.PICC_HaltA();
    rfid.PCD_StopCrypto1();

    delay(1000);

    return;
  }

  // ==========================================================
  // 3. HUELLA
  // ==========================================================

  // Si ya procesamos una huella, esperamos a que el dedo sea
  // retirado antes de permitir otra lectura. Esto evita que una
  // misma huella genere varios intentos consecutivos.
  if (esperandoRetiroHuella) {

    uint8_t estadoRetiro = finger.getImage();

    if (estadoRetiro == FINGERPRINT_NOFINGER) {
      esperandoRetiroHuella = false;
      Serial.println("Sensor listo para una nueva huella.");
    }

    delay(100);
    return;
  }

  int fingerprintID = obtenerHuella();

  // ----------------------------------------------------------
  // NO HAY DEDO
  // ----------------------------------------------------------

  if (fingerprintID == 0) {
    mostrarLCDSistemaListo();
    delay(100);
    return;
  }

  // ----------------------------------------------------------
  // HUELLA RECONOCIDA
  // ----------------------------------------------------------

  if (fingerprintID > 0) {

    mostrarLCD("HUELLA DETECTADA", "Verificando...");
    consultarAPIHuella(fingerprintID);

    esperandoRetiroHuella = true;

    delay(1500);

    return;
  }

  // ----------------------------------------------------------
  // HUELLA DETECTADA PERO NO RECONOCIDA
  // ----------------------------------------------------------

  if (fingerprintID == -1) {

    mostrarLCD("HUELLA NO VALIDA", "Verificando...");

    // -1 SOLO se devuelve despues de que:
    // 1. getImage() detecto una imagen valida.
    // 2. image2Tz() proceso correctamente la imagen.
    // 3. fingerSearch() confirmo que no existe coincidencia.
    //
    // Por eso este caso SI representa un intento de acceso
    // mediante una huella no registrada.

    consultarAPIHuella(-1);

    esperandoRetiroHuella = true;

    delay(1500);

    return;
  }

  // ----------------------------------------------------------
  // ERROR DE SENSOR / LECTURA
  // ----------------------------------------------------------

  // -2 NO es un intento fallido. No se llama a la API y no se
  // modifica el contador de intentos.
  if (fingerprintID == -2) {
    delay(150);
    return;
  }

  delay(100);
}

// ============================================================
// LEER RFID
// ============================================================

void leerRFID() {

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(
      rfid.uid.uidByte[i],
      HEX
    );

    if (i < rfid.uid.size - 1) {
      uid += ":";
    }
  }

  uid.toUpperCase();

  Serial.println();
  Serial.println("====================================");
  Serial.println("RFID DETECTADO");
  Serial.print("UID: ");
  Serial.println(uid);
  Serial.println("====================================");

  consultarAPIRFID(uid);
}

// ============================================================
// OBTENER HUELLA
// ============================================================

int obtenerHuella() {

  uint8_t p = finger.getImage();

  // ----------------------------------------------------------
  // No hay dedo en el sensor
  // ----------------------------------------------------------

  if (p == FINGERPRINT_NOFINGER) {
    return 0;
  }

  // ----------------------------------------------------------
  // Error al capturar la imagen
  // ----------------------------------------------------------

  if (p != FINGERPRINT_OK) {

    Serial.print("Error al capturar la huella. Codigo: ");
    Serial.println(p);

    // -2 = error del sensor/comunicacion, NO es un fallo de acceso.
    return -2;
  }

  Serial.println();
  Serial.println("Huella detectada.");
  Serial.println("Estabilizando lectura...");
  delay(ESPERA_ESTABILIZACION_HUELLA);

  // ----------------------------------------------------------
  // Convertir imagen a características
  // ----------------------------------------------------------

  p = finger.image2Tz();

  if (p != FINGERPRINT_OK) {

    Serial.print("No se pudo procesar la huella. Codigo: ");
    Serial.println(p);

    // -2 = error de lectura/procesamiento, NO es un fallo de acceso.
    return -2;
  }

  // ----------------------------------------------------------
  // Buscar coincidencia
  // ----------------------------------------------------------

  p = finger.fingerSearch();

  // ----------------------------------------------------------
  // HUELLA RECONOCIDA
  // ----------------------------------------------------------

  if (p == FINGERPRINT_OK) {

    Serial.println();
    Serial.println("====================================");
    Serial.println("HUELLA RECONOCIDA");
    Serial.print("ID: ");
    Serial.println(finger.fingerID);
    Serial.println("====================================");

    return finger.fingerID;
  }

  // ----------------------------------------------------------
  // HUELLA NO RECONOCIDA
  // ----------------------------------------------------------

  if (p == FINGERPRINT_NOTFOUND) {

    Serial.println();
    Serial.println("====================================");
    Serial.println("HUELLA NO RECONOCIDA");
    Serial.println("====================================");

    return -1;
  }

  // ----------------------------------------------------------
  // OTRO ERROR DEL SENSOR
  // ----------------------------------------------------------

  Serial.print("Error buscando la huella. Codigo: ");
  Serial.println(p);

  // -2 = error del sensor, NO es una huella no autorizada.
  return -2;
}

// ============================================================
// REGISTRAR FALLO DE ACCESO
// ============================================================

void registrarFallo(String metodo) {

  Serial.println();
  Serial.println("------------------------------------");
  Serial.print("ACCESO DENEGADO - ");
  Serial.println(metodo);
  Serial.println("------------------------------------");
  mostrarLCD("ACCESO DENEGADO", metodo);

  // El relé debe permanecer apagado
  digitalWrite(
    RELAY_PIN,
    RELAY_OFF
  );

  // ----------------------------------------------------------
  // Incrementar contador
  // ----------------------------------------------------------

  intentosFallidos++;

  Serial.print("Intento fallido: ");
  Serial.print(intentosFallidos);
  Serial.print("/");

  if (intentosMaximos > 0) {
    Serial.println(intentosMaximos);
  } else {
    Serial.println("?");
  }

  // ----------------------------------------------------------
  // Comprobar máximo de intentos
  // ----------------------------------------------------------

  if (
    intentosMaximos > 0 &&
    intentosFallidos >= intentosMaximos
  ) {

    activarBloqueo();
  }
  else if (intentosMaximos <= 0) {

    Serial.println(
      "AVISO: la API aun no ha enviado intentos_maximos."
    );

    Serial.println(
      "El contador se incremento, pero no se puede"
    );

    Serial.println(
      "activar el bloqueo hasta recibir la configuracion."
    );
  }
}

// ============================================================
// CONSULTAR API HUELLA
// ============================================================

void consultarAPIHuella(int fingerprintID) {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("ERROR: WiFi desconectado");
    mostrarLCD("ERROR WIFI", "Sin conexion");

    return;
  }

  HTTPClient http;

  http.begin(apiHuella);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  String json = "{";

  json += "\"dispositivo_id\":";
  json += String(dispositivoID);

  json += ",\"fingerprint_id\":";
  json += String(fingerprintID);

  json += "}";

  Serial.println("Enviando huella a API...");
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode > 0) {

    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    String respuesta = http.getString();

    Serial.println("Respuesta API:");
    Serial.println(respuesta);

    procesarRespuestaAPI(
      respuesta,
      "HUELLA"
    );

  } else {

    Serial.print("Error HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ============================================================
// CONSULTAR API RFID
// ============================================================

void consultarAPIRFID(String uid) {

  if (WiFi.status() != WL_CONNECTED) {

    Serial.println("ERROR: WiFi desconectado");
    mostrarLCD("ERROR WIFI", "Sin conexion");

    return;
  }

  HTTPClient http;

  http.begin(apiRFID);

  http.addHeader(
    "Content-Type",
    "application/json"
  );

  String json = "{";

  json += "\"dispositivo_id\":";
  json += String(dispositivoID);

  json += ",\"uid\":\"";
  json += uid;
  json += "\"";

  json += "}";

  Serial.println("Enviando RFID a API...");
  Serial.println(json);

  int httpCode = http.POST(json);

  if (httpCode > 0) {

    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    String respuesta = http.getString();

    Serial.println("Respuesta API:");
    Serial.println(respuesta);

    procesarRespuestaAPI(
      respuesta,
      "RFID"
    );

  } else {

    Serial.print("Error HTTP: ");
    Serial.println(http.errorToString(httpCode));
  }

  http.end();
}

// ============================================================
// PROCESAR RESPUESTA API
// ============================================================

void procesarRespuestaAPI(
  String respuesta,
  String metodo
) {

  DynamicJsonDocument doc(1024);

  DeserializationError error =
    deserializeJson(doc, respuesta);

  if (error) {

    Serial.print("Error JSON: ");
    Serial.println(error.c_str());

    return;
  }

  // ----------------------------------------------------------
  // Obtener información de la API
  // ----------------------------------------------------------

  bool acceso =
    doc["data"]["acceso"] | false;

  int tiempoApertura =
    doc["data"]["tiempo_apertura"] | 0;

  int nuevosIntentosMaximos =
    doc["data"]["intentos_maximos"] | 0;

  int nuevoTiempoBloqueo =
    doc["data"]["tiempo_bloqueo"] | 0;

  // ----------------------------------------------------------
  // Actualizar configuración
  // ----------------------------------------------------------

  if (nuevosIntentosMaximos > 0) {

    intentosMaximos =
      nuevosIntentosMaximos;
  }

  if (nuevoTiempoBloqueo > 0) {

    tiempoBloqueo =
      nuevoTiempoBloqueo;
  }

  // ----------------------------------------------------------
  // Mostrar configuración
  // ----------------------------------------------------------

  Serial.println();
  Serial.println("CONFIGURACION:");

  Serial.print("Intentos maximos: ");
  Serial.println(intentosMaximos);

  Serial.print("Tiempo bloqueo: ");
  Serial.print(tiempoBloqueo);
  Serial.println(" segundos");

  Serial.print("Intentos actuales: ");
  Serial.print(intentosFallidos);
  Serial.print("/");
  Serial.println(intentosMaximos);

  // ==========================================================
  // ACCESO AUTORIZADO
  // ==========================================================

  if (acceso) {

    Serial.println();
    Serial.println("************************************");
    Serial.print("ACCESO AUTORIZADO - ");
    Serial.println(metodo);
    Serial.println("************************************");

    // --------------------------------------------------------
    // Un acceso correcto reinicia los intentos
    // --------------------------------------------------------

    intentosFallidos = 0;

    Serial.println("Intentos fallidos reiniciados.");
    mostrarLCD("ACCESO AUTORIZADO", metodo);

    // --------------------------------------------------------
    // Verificar tiempo de apertura
    // --------------------------------------------------------

    if (tiempoApertura <= 0) {

      Serial.println(
        "ERROR: tiempo de apertura invalido"
      );
      mostrarLCD("ERROR APERTURA", "Config. invalida");

      return;
    }

    // --------------------------------------------------------
    // Activar relé
    // --------------------------------------------------------

    mostrarLCD("CERRADURA ABIERTA", "Acceso permitido");

    Serial.print("Abriendo cerradura durante ");
    Serial.print(tiempoApertura);
    Serial.println(" segundos...");

    digitalWrite(
      RELAY_PIN,
      RELAY_ON
    );

    delay(tiempoApertura * 1000);

    digitalWrite(
      RELAY_PIN,
      RELAY_OFF
    );

    Serial.println("Cerradura cerrada nuevamente.");
    mostrarLCDSistemaListo();
  }

  // ==========================================================
  // ACCESO DENEGADO
  // ==========================================================

  else {

    registrarFallo(metodo);
  }
}

// ============================================================
// ACTIVAR BLOQUEO
// ============================================================

void activarBloqueo() {

  sistemaBloqueado = true;

  inicioBloqueo = millis();

  Serial.println();
  Serial.println("####################################");
  Serial.println("       SISTEMA BLOQUEADO");
  mostrarLCD("SISTEMA BLOQUEADO", "Espere...");
  Serial.println("####################################");

  Serial.print("Tiempo de bloqueo: ");
  Serial.print(tiempoBloqueo);
  Serial.println(" segundos");

  Serial.println(
    "No se aceptaran RFID ni huellas."
  );
}

// ============================================================
// COMPROBAR BLOQUEO
// ============================================================

void comprobarBloqueo() {

  if (tiempoBloqueo <= 0) {

    sistemaBloqueado = false;
    intentosFallidos = 0;

    Serial.println(
      "Bloqueo finalizado."
    );
    mostrarLCDSistemaListo();

    return;
  }

  unsigned long tiempoTranscurrido =
    millis() - inicioBloqueo;

  unsigned long tiempoBloqueoMs =
    (unsigned long)tiempoBloqueo * 1000UL;

  // ----------------------------------------------------------
  // Mostrar tiempo restante aproximadamente cada segundo
  // ----------------------------------------------------------

  static unsigned long ultimaActualizacion = 0;

  if (
    millis() - ultimaActualizacion >= 1000
  ) {

    ultimaActualizacion = millis();

    unsigned long restante =
      tiempoBloqueoMs - tiempoTranscurrido;

    unsigned long segundosRestantes =
      restante / 1000;

    Serial.print(
      "Sistema bloqueado. Tiempo restante: "
    );

    Serial.print(
      segundosRestantes
    );

    Serial.println(" s");
  }

  // ----------------------------------------------------------
  // Terminar bloqueo
  // ----------------------------------------------------------

  if (
    tiempoTranscurrido >= tiempoBloqueoMs
  ) {

    sistemaBloqueado = false;

    intentosFallidos = 0;

    Serial.println();
    Serial.println("====================================");
    Serial.println("      BLOQUEO FINALIZADO");
    Serial.println("====================================");

    Serial.println(
      "Sistema nuevamente habilitado."
    );
    mostrarLCDSistemaListo();

    Serial.println(
      "Contador de intentos reiniciado."
    );

    Serial.println();
  }
}