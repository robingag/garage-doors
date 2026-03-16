/*
  ESP32 SLAVE - Garage Door Controller
  - Contrôle la Porte 2 directement (relais)
  - Communique avec ESP32 Master via Bluetooth Serial
  - Reçoit les commandes et renvoie le statut
*/

#include <BluetoothSerial.h>

// ==================== CONFIGURATION ====================
// Pins - Porte 2
const int RELAY_PIN_DOOR2  = 26;   // Relais pour actionner la porte 2
const int SENSOR_OPEN_2    = 32;   // Capteur porte ouverte (reed switch)
const int SENSOR_CLOSED_2  = 33;   // Capteur porte fermée (reed switch)

// Bluetooth
const char* BT_DEVICE_NAME = "GarageSlave";
// ========================================================

BluetoothSerial SerialBT;

String door2Status = "unknown";

// Pulse le relais pour simuler un appui bouton
void pulseDoor2() {
  Serial.println("[DOOR2] Activation relais...");
  digitalWrite(RELAY_PIN_DOOR2, HIGH);
  delay(500);
  digitalWrite(RELAY_PIN_DOOR2, LOW);
  Serial.println("[DOOR2] Relais désactivé.");
}

// Lit le statut de la porte 2 via les capteurs
String readDoor2Status() {
  bool isOpen   = digitalRead(SENSOR_OPEN_2) == LOW;
  bool isClosed = digitalRead(SENSOR_CLOSED_2) == LOW;

  if (isClosed) return "closed";
  if (isOpen)   return "open";
  return "moving";
}

// Envoie le statut au Master
void sendStatus() {
  door2Status = readDoor2Status();
  String msg = "STATUS:" + door2Status;
  SerialBT.println(msg);
  Serial.printf("[BT] Statut envoyé: %s\n", msg.c_str());
}

// Traite les commandes reçues du Master
void processCommand(String cmd) {
  cmd.trim();
  Serial.printf("[BT] Commande reçue: %s\n", cmd.c_str());

  if (cmd == "TOGGLE") {
    pulseDoor2();
    delay(1000);
    sendStatus();
  }
  else if (cmd == "OPEN") {
    door2Status = readDoor2Status();
    if (door2Status == "closed" || door2Status == "unknown") {
      pulseDoor2();
      delay(1000);
      sendStatus();
    } else {
      sendStatus(); // Déjà ouverte
    }
  }
  else if (cmd == "CLOSE") {
    door2Status = readDoor2Status();
    if (door2Status == "open" || door2Status == "unknown") {
      pulseDoor2();
      delay(1000);
      sendStatus();
    } else {
      sendStatus(); // Déjà fermée
    }
  }
  else if (cmd == "GET_STATUS") {
    sendStatus();
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n========= ESP32 SLAVE - Garage Door 2 Controller =========");

  // GPIO
  pinMode(RELAY_PIN_DOOR2, OUTPUT);
  digitalWrite(RELAY_PIN_DOOR2, LOW);
  pinMode(SENSOR_OPEN_2, INPUT_PULLUP);
  pinMode(SENSOR_CLOSED_2, INPUT_PULLUP);

  // Bluetooth en mode Slave
  SerialBT.begin(BT_DEVICE_NAME); // false = slave mode (défaut)
  Serial.println("[BT] Mode Slave démarré. En attente du Master...");
}

void loop() {
  // Lire les commandes du Master
  if (SerialBT.available()) {
    String cmd = SerialBT.readStringUntil('\n');
    processCommand(cmd);
  }

  // Mettre à jour le statut local périodiquement
  static unsigned long lastCheck = 0;
  if (millis() - lastCheck > 2000) {
    lastCheck = millis();
    String newStatus = readDoor2Status();
    if (newStatus != door2Status) {
      door2Status = newStatus;
      Serial.printf("[DOOR2] Changement de statut: %s\n", door2Status.c_str());
      // Envoyer le changement au Master automatiquement
      if (SerialBT.connected()) {
        sendStatus();
      }
    }
  }
}
