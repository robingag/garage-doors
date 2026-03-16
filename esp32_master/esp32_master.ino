/*
  ESP32 MASTER - Garage Door Controller
  - Connecté au WiFi et au broker MQTT
  - Contrôle la Porte 1 directement (relais)
  - Communique avec ESP32 Slave via Bluetooth Serial pour la Porte 2
  - Publie le statut des 2 portes sur MQTT
*/

#include <WiFi.h>
#include <PubSubClient.h>
#include <BluetoothSerial.h>

// ==================== CONFIGURATION ====================
// WiFi
const char* WIFI_SSID     = "VOTRE_SSID";
const char* WIFI_PASSWORD  = "VOTRE_MOT_DE_PASSE";

// MQTT Broker
const char* MQTT_SERVER   = "192.168.1.100";  // IP du broker MQTT
const int   MQTT_PORT     = 1883;
const char* MQTT_USER     = "";                // Laisser vide si pas d'auth
const char* MQTT_PASSWORD = "";

// Topics MQTT
const char* TOPIC_DOOR1_CMD    = "garage/door1/cmd";
const char* TOPIC_DOOR2_CMD    = "garage/door2/cmd";
const char* TOPIC_DOOR1_STATUS = "garage/door1/status";
const char* TOPIC_DOOR2_STATUS = "garage/door2/status";

// Pins - Porte 1
const int RELAY_PIN_DOOR1  = 26;   // Relais pour actionner la porte 1
const int SENSOR_OPEN_1    = 32;   // Capteur porte ouverte (reed switch)
const int SENSOR_CLOSED_1  = 33;   // Capteur porte fermée (reed switch)

// Bluetooth
const char* BT_DEVICE_NAME = "GarageMaster";
// ========================================================

BluetoothSerial SerialBT;
WiFiClient wifiClient;
PubSubClient mqtt(wifiClient);

String door1Status = "unknown";
String door2Status = "unknown";

unsigned long lastStatusPublish = 0;
const unsigned long STATUS_INTERVAL = 5000; // Publier le statut toutes les 5s

unsigned long lastBtCheck = 0;
const unsigned long BT_CHECK_INTERVAL = 1000;

// Pulse le relais pour simuler un appui bouton
void pulseDoor1() {
  Serial.println("[DOOR1] Activation relais...");
  digitalWrite(RELAY_PIN_DOOR1, HIGH);
  delay(500);
  digitalWrite(RELAY_PIN_DOOR1, LOW);
  Serial.println("[DOOR1] Relais désactivé.");
}

// Lit le statut de la porte 1 via les capteurs
String readDoor1Status() {
  bool isOpen   = digitalRead(SENSOR_OPEN_1) == LOW;   // Reed switch = LOW quand fermé
  bool isClosed = digitalRead(SENSOR_CLOSED_1) == LOW;

  if (isClosed) return "closed";
  if (isOpen)   return "open";
  return "moving";
}

// Envoie une commande à l'ESP32 Slave via Bluetooth
void sendBtCommand(const char* cmd) {
  if (SerialBT.connected()) {
    SerialBT.println(cmd);
    Serial.printf("[BT] Commande envoyée: %s\n", cmd);
  } else {
    Serial.println("[BT] Slave non connecté!");
  }
}

// Lit les messages du Slave via Bluetooth
void processBtMessages() {
  if (SerialBT.available()) {
    String msg = SerialBT.readStringUntil('\n');
    msg.trim();
    Serial.printf("[BT] Reçu du Slave: %s\n", msg.c_str());

    // Le slave envoie le statut de la porte 2
    if (msg.startsWith("STATUS:")) {
      door2Status = msg.substring(7);
      mqtt.publish(TOPIC_DOOR2_STATUS, door2Status.c_str(), true);
    }
  }
}

// Callback MQTT - réception de commandes
void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String message;
  for (unsigned int i = 0; i < length; i++) {
    message += (char)payload[i];
  }
  message.trim();

  Serial.printf("[MQTT] Topic: %s | Message: %s\n", topic, message.c_str());

  // Commande Porte 1
  if (String(topic) == TOPIC_DOOR1_CMD) {
    if (message == "TOGGLE") {
      pulseDoor1();
    } else if (message == "OPEN") {
      if (door1Status == "closed" || door1Status == "unknown") {
        pulseDoor1();
      }
    } else if (message == "CLOSE") {
      if (door1Status == "open" || door1Status == "unknown") {
        pulseDoor1();
      }
    }
  }

  // Commande Porte 2 -> relayée au Slave via Bluetooth
  if (String(topic) == TOPIC_DOOR2_CMD) {
    if (message == "TOGGLE") {
      sendBtCommand("TOGGLE");
    } else if (message == "OPEN") {
      sendBtCommand("OPEN");
    } else if (message == "CLOSE") {
      sendBtCommand("CLOSE");
    }
  }
}

void connectWiFi() {
  Serial.printf("[WIFI] Connexion à %s", WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }
  Serial.printf("\n[WIFI] Connecté! IP: %s\n", WiFi.localIP().toString().c_str());
}

void connectMQTT() {
  while (!mqtt.connected()) {
    Serial.println("[MQTT] Connexion au broker...");
    String clientId = "GarageMaster-" + String(random(0xffff), HEX);

    bool connected;
    if (strlen(MQTT_USER) > 0) {
      connected = mqtt.connect(clientId.c_str(), MQTT_USER, MQTT_PASSWORD);
    } else {
      connected = mqtt.connect(clientId.c_str());
    }

    if (connected) {
      Serial.println("[MQTT] Connecté!");
      mqtt.subscribe(TOPIC_DOOR1_CMD);
      mqtt.subscribe(TOPIC_DOOR2_CMD);
      // Publier statut initial
      mqtt.publish(TOPIC_DOOR1_STATUS, door1Status.c_str(), true);
      mqtt.publish(TOPIC_DOOR2_STATUS, door2Status.c_str(), true);
    } else {
      Serial.printf("[MQTT] Échec (rc=%d). Retry dans 5s...\n", mqtt.state());
      delay(5000);
    }
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println("\n========= ESP32 MASTER - Garage Door Controller =========");

  // GPIO
  pinMode(RELAY_PIN_DOOR1, OUTPUT);
  digitalWrite(RELAY_PIN_DOOR1, LOW);
  pinMode(SENSOR_OPEN_1, INPUT_PULLUP);
  pinMode(SENSOR_CLOSED_1, INPUT_PULLUP);

  // Bluetooth
  SerialBT.begin(BT_DEVICE_NAME, true); // true = master mode
  Serial.println("[BT] Mode Master démarré. En attente du Slave...");

  // WiFi
  connectWiFi();

  // MQTT
  mqtt.setServer(MQTT_SERVER, MQTT_PORT);
  mqtt.setCallback(mqttCallback);
  connectMQTT();

  // Connexion Bluetooth au Slave
  Serial.println("[BT] Recherche du Slave 'GarageSlave'...");
  bool btConnected = SerialBT.connect("GarageSlave");
  if (btConnected) {
    Serial.println("[BT] Connecté au Slave!");
  } else {
    Serial.println("[BT] Impossible de se connecter au Slave. Réessai au prochain cycle.");
  }
}

void loop() {
  // Maintenir les connexions
  if (WiFi.status() != WL_CONNECTED) {
    connectWiFi();
  }
  if (!mqtt.connected()) {
    connectMQTT();
  }
  mqtt.loop();

  // Reconnecter Bluetooth si déconnecté
  if (!SerialBT.connected()) {
    if (millis() - lastBtCheck > 10000) { // Retry toutes les 10s
      lastBtCheck = millis();
      Serial.println("[BT] Tentative de reconnexion au Slave...");
      SerialBT.connect("GarageSlave");
    }
  }

  // Lire messages Bluetooth du Slave
  processBtMessages();

  // Publier statut périodiquement
  if (millis() - lastStatusPublish > STATUS_INTERVAL) {
    lastStatusPublish = millis();

    // Statut porte 1
    String newStatus = readDoor1Status();
    if (newStatus != door1Status) {
      door1Status = newStatus;
      Serial.printf("[DOOR1] Nouveau statut: %s\n", door1Status.c_str());
    }
    mqtt.publish(TOPIC_DOOR1_STATUS, door1Status.c_str(), true);

    // Demander le statut au Slave
    if (SerialBT.connected()) {
      SerialBT.println("GET_STATUS");
    }
  }
}
