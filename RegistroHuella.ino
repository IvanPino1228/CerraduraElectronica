#include <Adafruit_Fingerprint.h>

#define RX_PIN 16
#define TX_PIN 17

HardwareSerial mySerial(2);

Adafruit_Fingerprint finger = Adafruit_Fingerprint(&mySerial);

uint8_t id = 1;

void setup() {

  Serial.begin(115200);

  mySerial.begin(
    57600,
    SERIAL_8N1,
    RX_PIN,
    TX_PIN
  );

  Serial.println();
  Serial.println("Iniciando AS608...");

  finger.begin(57600);

  if (finger.verifyPassword()) {

    Serial.println("AS608 detectado correctamente.");

  } else {

    Serial.println("No se pudo detectar el AS608.");

    while (1) {
      delay(1000);
    }
  }

  Serial.println();
  Serial.print("Registrando huella con ID: ");
  Serial.println(id);

  registrarHuella(id);
}

void loop() {
}

void registrarHuella(uint8_t id) {

  int p = -1;

  Serial.println();
  Serial.println("Coloca el dedo sobre el sensor.");

  while (p != FINGERPRINT_OK) {

    p = finger.getImage();

    if (p == FINGERPRINT_NOFINGER) {
      delay(100);
    }

    else if (p == FINGERPRINT_OK) {
      Serial.println("Huella capturada.");
    }

    else {
      Serial.println("Error capturando la huella.");
      return;
    }
  }

  p = finger.image2Tz(1);

  if (p != FINGERPRINT_OK) {

    Serial.println("No se pudo convertir la primera imagen.");
    return;
  }

  Serial.println("Retira el dedo.");

  delay(2000);

  while (finger.getImage() != FINGERPRINT_NOFINGER) {
    delay(100);
  }

  Serial.println();
  Serial.println("Coloca nuevamente el mismo dedo.");

  p = -1;

  while (p != FINGERPRINT_OK) {

    p = finger.getImage();

    if (p == FINGERPRINT_NOFINGER) {
      delay(100);
    }

    else if (p == FINGERPRINT_OK) {
      Serial.println("Segunda huella capturada.");
    }

    else {
      Serial.println("Error capturando la segunda huella.");
      return;
    }
  }

  p = finger.image2Tz(2);

  if (p != FINGERPRINT_OK) {

    Serial.println("No se pudo convertir la segunda imagen.");
    return;
  }

  Serial.println("Comparando las dos huellas...");

  p = finger.createModel();

  if (p != FINGERPRINT_OK) {

    Serial.println("Las huellas no coinciden.");
    return;
  }

  Serial.println("Huella coincidente.");

  p = finger.storeModel(id);

  if (p == FINGERPRINT_OK) {

    Serial.println();
    Serial.println("==============================");
    Serial.println("HUELLA REGISTRADA CORRECTAMENTE");
    Serial.print("ID: ");
    Serial.println(id);
    Serial.println("==============================");

  } else {

    Serial.println("No se pudo guardar la huella.");
  }
}