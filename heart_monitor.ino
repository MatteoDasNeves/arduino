#include <Arduino.h>
#include <Wire.h>
#include <U8g2lib.h>
#include "DFRobot_BloodOxygen_S.h"

// =====================================================
// MQTT (décommenter une fois WiFi/Ethernet configuré)
// =====================================================
// #include <WiFi.h>
// #include <PubSubClient.h>

// =====================================================
// TYPE DE BUZZER
// =====================================================
#define BUZZER_ACTIF 0

const unsigned int frequenceBip    = 1000;
const unsigned int frequenceAlarme = 2000;

// =====================================================
// OLED
// =====================================================

U8G2_SSD1306_128X64_NONAME_1_HW_I2C u8g2(
  U8G2_R0,
  U8X8_PIN_NONE
);

// =====================================================
// CAPTEUR DFRobot
// =====================================================

#define I2C_ADDRESS 0x57
DFRobot_BloodOxygen_S_I2C MAX30102(&Wire, I2C_ADDRESS);

// =====================================================
// BROCHES
// =====================================================

const int motorPin    = 9;
const int buttonPin   = 2;
const int buzzerPin   = 8;
const int ledRougePin = 10;
const int ledVertePin = 11;

// =====================================================
// SEUILS DU RYTHME CARDIAQUE (adulte au repos)
// =====================================================

const int bpmMinNormal  = 60;
const int bpmMaxNormal  = 100;
const int spo2MinNormal = 95;

// =====================================================
// CAPTEUR
// =====================================================

int bpm  = -1;
int spo2 = -1;
float temperature = 0;

unsigned long derniereMesure = 0;
const unsigned long intervalleMesure = 4000;

// =====================================================
// ETAT DU SYSTEME
// =====================================================

bool rythmeDejaDetecte = false;
bool alarmeActive      = false;

// =====================================================
// MOTEUR
// =====================================================

bool moteurActive = true;
bool impulsionMoteur = false;
unsigned long debutImpulsionMoteur = 0;
const unsigned long dureeImpulsionMoteur = 150;
const int puissanceMoteur = 200;

// =====================================================
// BOUTON
// =====================================================

int ancienEtatBouton = LOW;
unsigned long dernierAppui = 0;
const unsigned long antiRebond = 200;

// =====================================================
// BATTEMENTS / BUZZER
// =====================================================

unsigned long dernierBattement = 0;
const unsigned long dureeBip = 70;

#if BUZZER_ACTIF
bool bipCourtEnCours = false;
unsigned long debutBip = 0;
#endif

bool sonAlarmeEnCours = false;

// =====================================================
// LED
// =====================================================

bool flashLedEnCours = false;
unsigned long debutFlashLed = 0;
const unsigned long dureeFlashLed = 100;

unsigned long dernierClignotement = 0;
bool etatClignotement = false;
const unsigned long periodeAlarmeLed  = 150;
const unsigned long periodeAttenteLed = 1000;

// =====================================================
// MQTT (décommenter une fois WiFi OK)
// =====================================================
/*
WiFiClient wifiClient;
PubSubClient mqttClient(wifiClient);

const char* MQTT_BROKER = "mosquitto.org";
const int MQTT_PORT = 1883;
const char* TOPIC_SENSOR_RAW = "heart/sensor/raw";
const char* TOPIC_DISPLAY_BPM = "heart/display/bpm";
const char* MQTT_CLIENT_ID = "ArduinoHeartMonitor_001";

int bpmMQTT = -1;
unsigned long dernierPublishMQTT = 0;
const unsigned long intervalePublishMQTT = 4000;

void onMQTTMessage(char* topic, byte* payload, unsigned int length) {
  if (strcmp(topic, TOPIC_DISPLAY_BPM) != 0) return;

  char message[50];
  strncpy(message, (char*)payload, length);
  message[length] = '\0';

  int bpm_lu = atoi(message);
  if (bpm_lu > 0 && bpm_lu <= 220) {
    bpmMQTT = bpm_lu;
    Serial.print(F("BPM MQTT : "));
    Serial.println(bpmMQTT);
  }
}

void connecterMQTT() {
  while (!mqttClient.connected()) {
    Serial.print(F("MQTT connexion... "));

    if (mqttClient.connect(MQTT_CLIENT_ID)) {
      Serial.println(F("OK"));
      mqttClient.subscribe(TOPIC_DISPLAY_BPM);
    } else {
      Serial.println(F("Echec"));
      delay(5000);
    }
  }
}

void publierSenseurMQTT() {
  if (!mqttClient.connected()) {
    connecterMQTT();
    return;
  }

  char payload[100];
  snprintf(payload, sizeof(payload),
    "{\"bpm\":%d,\"spo2\":%d,\"temp\":%.1f}",
    bpm, spo2, temperature
  );

  if (mqttClient.publish(TOPIC_SENSOR_RAW, payload)) {
    Serial.print(F("Publie : "));
    Serial.println(payload);
  }
}
*/

// =====================================================
// FONCTIONS BUZZER
// =====================================================

void buzzerBip() {
#if BUZZER_ACTIF
  digitalWrite(buzzerPin, HIGH);
  bipCourtEnCours = true;
  debutBip = millis();
#else
  tone(buzzerPin, frequenceBip, dureeBip);
#endif
}

void buzzerAlarmeOn() {
  if (sonAlarmeEnCours) return;

#if BUZZER_ACTIF
  digitalWrite(buzzerPin, HIGH);
  bipCourtEnCours = false;
#else
  tone(buzzerPin, frequenceAlarme);
#endif

  sonAlarmeEnCours = true;
}

void buzzerStop() {
#if BUZZER_ACTIF
  digitalWrite(buzzerPin, LOW);
  bipCourtEnCours = false;
#else
  noTone(buzzerPin);
  digitalWrite(buzzerPin, LOW);
#endif

  sonAlarmeEnCours = false;
}

void gererFinBip() {
#if BUZZER_ACTIF
  if (bipCourtEnCours && millis() - debutBip >= dureeBip) {
    digitalWrite(buzzerPin, LOW);
    bipCourtEnCours = false;
  }
#endif
}

// =====================================================
// MOTEUR
// =====================================================

void arreterMoteur() {
  analogWrite(motorPin, 0);
  impulsionMoteur = false;
}

// =====================================================
// MESURE VALIDE ?
// =====================================================

bool mesureEstValide() {
  return bpm >= 30 && bpm <= 220 && spo2 > 0 && spo2 <= 100;
}

bool rythmeNormal() {
  return bpm >= bpmMinNormal &&
         bpm <= bpmMaxNormal &&
         spo2 >= spo2MinNormal;
}

// =====================================================
// LED
// =====================================================

void eteindreLeds() {
  digitalWrite(ledRougePin, LOW);
  digitalWrite(ledVertePin, LOW);
  flashLedEnCours = false;
}

void flashLedBattement() {
  if (rythmeNormal()) {
    digitalWrite(ledVertePin, HIGH);
    digitalWrite(ledRougePin, LOW);
  } else {
    digitalWrite(ledRougePin, HIGH);
    digitalWrite(ledVertePin, LOW);
  }
  flashLedEnCours = true;
  debutFlashLed = millis();
}

void gererFinFlashLed() {
  if (flashLedEnCours && millis() - debutFlashLed >= dureeFlashLed) {
    eteindreLeds();
  }
}

void clignoterLed(int pinActive, int pinEteinte, unsigned long periode) {
  digitalWrite(pinEteinte, LOW);
  if (millis() - dernierClignotement >= periode) {
    dernierClignotement = millis();
    etatClignotement = !etatClignotement;
    digitalWrite(pinActive, etatClignotement ? HIGH : LOW);
  }
}

// =====================================================
// OLED
// =====================================================

void afficherOLED() {

  char texteBPM[16];
  char texteSPO2[16];
  const char* texteEtat;

  if (bpm > 0) {
    snprintf(texteBPM, sizeof(texteBPM), "BPM : %d", bpm);
  } else {
    snprintf(texteBPM, sizeof(texteBPM), "BPM : --");
  }

  if (spo2 > 0) {
    snprintf(texteSPO2, sizeof(texteSPO2), "SpO2 : %d%%", spo2);
  } else {
    snprintf(texteSPO2, sizeof(texteSPO2), "SpO2 : --");
  }

  if (alarmeActive) {
    texteEtat = "!!! ALARME !!!";
  } else if (mesureEstValide() && !rythmeNormal()) {
    texteEtat = "Rythme ANORMAL";
  } else if (moteurActive) {
    texteEtat = "Moteur : ON";
  } else {
    texteEtat = "Moteur : OFF";
  }

  u8g2.firstPage();
  do {
    u8g2.setFont(u8g2_font_ncenB08_tr);
    u8g2.drawStr(0, 10, "HEART MONITOR");
    u8g2.drawHLine(0, 13, 128);
    u8g2.drawStr(0, 28, texteBPM);
    u8g2.drawStr(0, 44, texteSPO2);
    u8g2.drawStr(0, 61, texteEtat);
  } while (u8g2.nextPage());
}

// =====================================================
// LECTURE CAPTEUR
// =====================================================

void lireCapteurCardiaque() {

  MAX30102.getHeartbeatSPO2();

  bpm         = MAX30102._sHeartbeatSPO2.Heartbeat;
  spo2        = MAX30102._sHeartbeatSPO2.SPO2;
  temperature = MAX30102.getTemperature_C();

  // Publier sur MQTT (décommenter une fois MQTT OK)
  // publierSenseurMQTT();

  Serial.println();
  Serial.println(F("======================"));
  Serial.print(F("SPO2 is : "));       Serial.print(spo2); Serial.println(F("%"));
  Serial.print(F("heart rate is : ")); Serial.print(bpm);  Serial.println(F(" Times/min"));
  Serial.print(F("Temperature : "));   Serial.print(temperature); Serial.println(F(" C"));

  if (mesureEstValide()) {

    rythmeDejaDetecte = true;

    if (alarmeActive) {
      Serial.println(F("SIGNAL RETROUVE -> FIN ALARME"));
      alarmeActive = false;
      buzzerStop();
      eteindreLeds();
      dernierBattement = millis();
    }

    Serial.println(F("Mesure valide"));
    Serial.println(rythmeNormal() ? F("Rythme NORMAL (LED verte)")
                                  : F("Rythme ANORMAL (LED rouge)"));

  } else {

    Serial.println(F("Mesure invalide"));
    arreterMoteur();

    if (rythmeDejaDetecte) {
      if (!alarmeActive) {
        Serial.println(F("!!! PERTE DU SIGNAL -> ALARME !!!"));
      }
      alarmeActive = true;
      buzzerAlarmeOn();
    } else {
      alarmeActive = false;
      buzzerStop();
      Serial.println(F("En attente du premier signal..."));
    }
  }

  Serial.println(F("======================"));

  afficherOLED();
}

// =====================================================
// BATTEMENT NORMAL
// =====================================================

void declencherBattement() {

  buzzerBip();
  flashLedBattement();

  if (moteurActive) {
    analogWrite(motorPin, puissanceMoteur);
    impulsionMoteur = true;
    debutImpulsionMoteur = millis();
  }

  Serial.print(F("BIP -> "));
  Serial.print(bpm);
  Serial.println(F(" BPM"));
}

// =====================================================
// GESTION BATTEMENTS
// =====================================================

void gererBattements() {

  if (alarmeActive) {
    buzzerAlarmeOn();
    arreterMoteur();
    clignoterLed(ledRougePin, ledVertePin, periodeAlarmeLed);
    return;
  }

  if (!mesureEstValide()) {
    arreterMoteur();
    clignoterLed(ledVertePin, ledRougePin, periodeAttenteLed);
    return;
  }

  unsigned long intervalleBattement = 60000UL / bpm;

  if (millis() - dernierBattement >= intervalleBattement) {
    dernierBattement = millis();
    declencherBattement();
  }

  gererFinBip();
  gererFinFlashLed();

  if (impulsionMoteur &&
      millis() - debutImpulsionMoteur >= dureeImpulsionMoteur) {
    arreterMoteur();
  }
}

// =====================================================
// BOUTON (contrôle uniquement le moteur)
// =====================================================

void gererBouton() {

  int etatBouton = digitalRead(buttonPin);

  if (etatBouton == HIGH &&
      ancienEtatBouton == LOW &&
      millis() - dernierAppui >= antiRebond) {

    dernierAppui = millis();
    moteurActive = !moteurActive;

    if (moteurActive) {
      Serial.println(F("MOTEUR ACTIVE"));
    } else {
      Serial.println(F("MOTEUR DESACTIVE"));
      arreterMoteur();
    }

    afficherOLED();
  }

  ancienEtatBouton = etatBouton;
}

// =====================================================
// SETUP
// =====================================================

void setup() {

  Serial.begin(115200);

  pinMode(motorPin, OUTPUT);
  analogWrite(motorPin, 0);

  pinMode(buzzerPin, OUTPUT);
  digitalWrite(buzzerPin, LOW);

  pinMode(ledRougePin, OUTPUT);
  pinMode(ledVertePin, OUTPUT);
  eteindreLeds();

  pinMode(buttonPin, INPUT);

  Wire.begin();
  Wire.setClock(100000);

  // --- Capteur ---
  while (!MAX30102.begin()) {
    Serial.println(F("init fail!"));
    delay(1000);
  }
  Serial.println(F("init success!"));
  Serial.println(F("start measuring..."));
  MAX30102.sensorStartCollect();

  // --- OLED ---
  u8g2.setBusClock(100000);
  u8g2.begin();
  afficherOLED();

  // --- WiFi / MQTT (décommenter) ---
  /*
  Serial.print(F("WiFi : "));
  Serial.println(WIFI_SSID);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);

  int tentatives = 0;
  while (WiFi.status() != WL_CONNECTED && tentatives < 20) {
    delay(500);
    Serial.print(".");
    tentatives++;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.println(F("\nWiFi OK"));
    mqttClient.setServer(MQTT_BROKER, MQTT_PORT);
    mqttClient.setCallback(onMQTTMessage);
    connecterMQTT();
  } else {
    Serial.println(F("\nWiFi FAIL"));
  }
  */

  derniereMesure   = millis();
  dernierBattement = millis();

  Serial.println();
  Serial.println(F("Systeme pret !"));
  Serial.println(F("Posez le doigt sur le capteur."));
}

// =====================================================
// LOOP
// =====================================================

void loop() {

  // Garder MQTT vivant (décommenter)
  // if (WiFi.status() == WL_CONNECTED) {
  //   if (!mqttClient.connected()) {
  //     connecterMQTT();
  //   }
  //   mqttClient.loop();
  // }

  gererBouton();
  gererBattements();

  if (millis() - derniereMesure >= intervalleMesure) {
    derniereMesure = millis();
    lireCapteurCardiaque();
  }
}
