#include <WiFi.h>
#include <HTTPClient.h>
#include <SPI.h>
#include <MFRC522.h>

// =========================
// WIFI
// =========================
const char* ssid = "FLIA PINO MORENO";
const char* password = "33253753";

// =========================
// API
// =========================
const char* apiURL =
  "http://192.168.1.10/cerradura-api/api/rfid.php";

const int dispositivoID = 1;

// =========================
// RC522
// =========================
#define SS_PIN  5
#define RST_PIN 21

MFRC522 rfid(SS_PIN, RST_PIN);

// =========================
// SETUP
// =========================
void setup() {

  Serial.begin(115200);
  delay(1000);

  Serial.println();
  Serial.println("================================");
  Serial.println("   SISTEMA DE ACCESO");
  Serial.println("================================");

  // -------------------------
  // WiFi
  // -------------------------
  Serial.println("Conectando al WiFi...");

  WiFi.begin(ssid, password);

  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println();
  Serial.println("WiFi conectado");

  Serial.print("IP del ESP32: ");
  Serial.println(WiFi.localIP());

  // -------------------------
  // SPI
  // -------------------------
  SPI.begin(18, 19, 23, 5);

  // -------------------------
  // RC522
  // -------------------------
  rfid.PCD_Init();

  delay(100);

  Serial.println("RC522 iniciado");
  Serial.println();
  Serial.println("Acerque una tarjeta...");
}

// =========================
// LOOP
// =========================
void loop() {

  // No hay tarjeta nueva
  if (!rfid.PICC_IsNewCardPresent()) {
    return;
  }

  // No se pudo leer la tarjeta
  if (!rfid.PICC_ReadCardSerial()) {
    return;
  }

  // -------------------------
  // Obtener UID
  // -------------------------

  String uid = "";

  for (byte i = 0; i < rfid.uid.size; i++) {

    if (i > 0) {
      uid += ":";
    }

    if (rfid.uid.uidByte[i] < 0x10) {
      uid += "0";
    }

    uid += String(rfid.uid.uidByte[i], HEX);
  }

  uid.toUpperCase();

  Serial.println();
  Serial.println("-------------------------------");
  Serial.print("UID detectado: ");
  Serial.println(uid);

  // -------------------------
  // Enviar a API
  // -------------------------

  if (WiFi.status() == WL_CONNECTED) {

    HTTPClient http;

    http.begin(apiURL);

    http.addHeader("Content-Type", "application/json");

    String json = "{";
    json += "\"dispositivo_id\":";
    json += dispositivoID;
    json += ",";
    json += "\"uid\":\"";
    json += uid;
    json += "\"";
    json += "}";

    Serial.print("Enviando: ");
    Serial.println(json);

    int httpCode = http.POST(json);

    Serial.print("HTTP Code: ");
    Serial.println(httpCode);

    if (httpCode > 0) {

      String response = http.getString();

      Serial.println("Respuesta de la API:");
      Serial.println(response);

    } else {

      Serial.print("Error HTTP: ");
      Serial.println(http.errorToString(httpCode));
    }

    http.end();

  } else {

    Serial.println("WiFi desconectado");
  }

  // Detener comunicación con la tarjeta
  rfid.PICC_HaltA();
  rfid.PCD_StopCrypto1();

  delay(2000);

  Serial.println();
  Serial.println("Acerque una tarjeta...");
}