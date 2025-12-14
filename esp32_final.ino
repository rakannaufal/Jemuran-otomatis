#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <DHT.h>
#include <Preferences.h>

// === ANTI REBOOT ===
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "esp_wifi.h"

// ===== WiFi Configuration =====
const char* ssid = "Afiqa 08";
const char* password = "Naufal03";

// ===== MQTT Configuration =====
const char* mqtt_server = "broker.hivemq.com"; 
const char* mqtt_topic_data = "jemuran_otomatis/sensor/data";
const char* mqtt_topic_command = "jemuran_otomatis/sensor/command";
const char* device_id = "JEMURAN_001";

WiFiClient espClient;
PubSubClient client(espClient);

// ===== Preferences =====
Preferences preferences;

// ===== Pin Configuration =====
const int pinServo = 13;
const int pinHujan = 25;
const int pinCahaya = 26;
const int pinDHT = 4;
const int pinBuzzer = 14;
const int pinDinamo1 = 27;
const int pinDinamo2 = 12;

// ===== DHT Configuration =====
#define DHTTYPE DHT11
DHT dht(pinDHT, DHTTYPE);

// ===== Servo =====
Servo atapServo;
const int POSISI_TERTUTUP = 0;
const int POSISI_TERBUKA = 90;

// ===== Variables =====
int currentServoPosition = 0;
int targetServoPosition = 0;
bool isAutoMode = true;
bool atapTerbuka = false;
bool isBuzzerActive = false;

// Dinamo
unsigned long dinamoStartTime = 0;
bool isDinamoMoving = false;
const long dinamoDuration = 2000;  // Durasi dinamo bergerak (ms)
String dinamoStatus = "STOP";

// Servo movement
bool isServoMoving = false;
unsigned long lastServoMoveTime = 0;
const int SERVO_STEP = 3;
const int SERVO_DELAY = 25;

// === LOGIKA BARU: INTERUPSI DINAMO ===
bool posisiJemuranDiLuar = false; 
unsigned long servoStopTimestamp = 0; 
unsigned long dinamoStopTimestamp = 0; // Timer setelah dinamo di-stop paksa
const long delayStabilisasi = 1000; 

// Sensor readings
int statusHujan = HIGH;
int statusCahaya = LOW;
float suhu = 0.0;
float kelembaban = 0.0;

// Fuzzy variables
float uSuhuDingin = 0.0, uSuhuPanas = 0.0;
float uLembabKering = 0.0, uLembabBasah = 0.0;
float uHujanYa = 0.0, uHujanTidak = 0.0;
float uCahayaTerang = 0.0, uCahayaGelap = 0.0;

String fuzzyStatusCondition = "Menunggu...";
String currentRoofStatus = "TERTUTUP";

// === TIMING ===
unsigned long previousMillis = 0;
const long interval = 500;
unsigned long lastDHTReadTime = 0;
const long intervalDHT = 2000;
unsigned long lastMQTTPublish = 0;
const long mqttPublishInterval = 1000;
unsigned long lastWiFiCheck = 0;
const long wifiCheckInterval = 15000;
unsigned long lastMQTTReconnect = 0;
const long mqttReconnectInterval = 5000;
unsigned long lastSerialPrint = 0;
const long serialPrintInterval = 2000; // Serial lebih cepat (2 detik)

// State
bool wifiConnected = false;
bool mqttConnected = false;
bool hasPlayedConnectionBeep = false;

void setup() {
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  esp_task_wdt_deinit();
  
  Serial.begin(115200);
  delay(2000);

  Serial.println("\n==========================================");
  Serial.println("   JEMURAN OTOMATIS");
  Serial.println("==========================================");

  setCpuFrequencyMhz(80);

  // Load memori
  preferences.begin("jemuran", false);
  currentServoPosition = preferences.getInt("servoPos", 0);
  posisiJemuranDiLuar = preferences.getBool("posisiJemuranDiLuar", false);
  
  Serial.print("Last Pos Servo: "); Serial.println(currentServoPosition);
  Serial.print("Last Pos Dinamo: "); Serial.println(posisiJemuranDiLuar ? "LUAR" : "DALAM");

  pinMode(pinHujan, INPUT); pinMode(pinCahaya, INPUT); pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinBuzzer, LOW);
  
  // Setup Dinamo pins
  pinMode(pinDinamo1, OUTPUT);
  pinMode(pinDinamo2, OUTPUT);
  stopDinamo();

  dht.begin();
  delay(500);

  // Servo
  atapServo.setPeriodHertz(50);
  atapServo.attach(pinServo, 500, 2400);
  delay(300);
  atapServo.write(currentServoPosition);
  targetServoPosition = currentServoPosition;
  atapTerbuka = (currentServoPosition > 10);
  currentRoofStatus = atapTerbuka ? "TERBUKA" : "TERTUTUP";
  
  setup_wifi();
  client.setBufferSize(256);
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  Serial.println("\n=== SISTEM SIAP ===\n");
}

void loop() {
  unsigned long currentMillis = millis();
  yield();
  
  handleNetwork(currentMillis);
  bool sensorChanged = readSensors();
  
  // === LOGIC UPDATE ===
  if ((currentMillis - previousMillis >= interval) || sensorChanged) {
    previousMillis = currentMillis;
    
    // 1. Fuzzy Logic
    processFuzzyLogic();
    
    // 2. Tentukan Target (Auto Mode)
    if (isAutoMode) {
        if (atapTerbuka) updateTargetServo(POSISI_TERBUKA);
        else updateTargetServo(POSISI_TERTUTUP);
    }

    updateBuzzer();
    if (sensorChanged && mqttConnected) {
      sendSensorDataMQTT();
      lastMQTTPublish = currentMillis;
    }
  }

  // === EKSEKUSI GERAKAN ===
  
  // A. Cek Interupsi Dinamo (PENTING: Cek ini duluan)
  checkDinamoInterrupt();

  // B. Gerakkan Servo
  executeServoMove();
  
  // C. Logika Dinamo Normal
  checkAndMoveDinamo();
  
  // D. Timer Stop Dinamo
  updateDinamoStop();
  
  // === SERIAL PRINT LENGKAP ===
  if (currentMillis - lastSerialPrint >= serialPrintInterval) {
    lastSerialPrint = currentMillis;
    printFullStatus();
  }
  
  // MQTT Periodic
  if (mqttConnected && (currentMillis - lastMQTTPublish >= mqttPublishInterval)) {
    lastMQTTPublish = currentMillis;
    sendSensorDataMQTT();
  }
  delay(20);
}

// ==========================================
//          LOGIKA INTERUPSI DINAMO (BARU)
// ==========================================

void checkDinamoInterrupt() {
  // Hanya berlaku di mode Auto dan Dinamo sedang gerak
  if (isAutoMode && isDinamoMoving) {
    
    // KASUS 1: Dinamo sedang MUNDUR (Menutup karena Hujan), tapi tiba-tiba HARUS BUKA
    if (dinamoStatus == "MUNDUR" && atapTerbuka == true) {
      Serial.println(">>> INTERUPSI: Dinamo Stop! Hujan Berhenti -> Balik Buka!");
      stopDinamo(); // Stop paksa
      
      // Reset timer agar ada jeda sebelum servo gerak
      dinamoStopTimestamp = millis();
      
      // Paksa servo target ke BUKA (biar nanti servo gerak duluan)
      targetServoPosition = POSISI_TERBUKA;
    }
    
    // KASUS 2: Dinamo sedang MAJU (Membuka), tapi tiba-tiba HARUS TUTUP
    else if (dinamoStatus == "MAJU" && atapTerbuka == false) {
      Serial.println(">>> INTERUPSI: Dinamo Stop! Hujan Turun -> Balik Tutup!");
      stopDinamo();
      dinamoStopTimestamp = millis();
      targetServoPosition = POSISI_TERTUTUP;
    }
  }
}

// ==========================================
//          SERVO CONTROL
// ==========================================

void updateTargetServo(int newPos) {
  if (targetServoPosition != newPos) {
    targetServoPosition = newPos;
    isServoMoving = true; 
  }
}

void executeServoMove() {
  // Servo boleh gerak jika dinamo TIDAK sedang jalan
  // DAN sudah ada jeda setelah dinamo di-stop paksa (Interupsi)
  if (!isDinamoMoving && (millis() - dinamoStopTimestamp > delayStabilisasi)) {
    
    if (currentServoPosition != targetServoPosition) {
      isServoMoving = true; 
      unsigned long currentMillis = millis();
      if (currentMillis - lastServoMoveTime >= SERVO_DELAY) {
        lastServoMoveTime = currentMillis;
        if (currentServoPosition < targetServoPosition) {
          currentServoPosition += SERVO_STEP;
          if (currentServoPosition > targetServoPosition) currentServoPosition = targetServoPosition;
        } else {
          currentServoPosition -= SERVO_STEP;
          if (currentServoPosition < targetServoPosition) currentServoPosition = targetServoPosition;
        }
        atapServo.write(currentServoPosition);
      }
    } 
    else {
      if (isServoMoving) {
        isServoMoving = false;
        servoStopTimestamp = millis(); // Catat waktu servo berhenti
        preferences.putInt("servoPos", currentServoPosition);
        Serial.println(">> Servo Sampai Target.");
      }
    }
  }
  currentRoofStatus = (currentServoPosition > 10) ? "TERBUKA" : "TERTUTUP";
}

// ==========================================
//     LOGIKA DINAMO (NORMAL)
// ==========================================

void checkAndMoveDinamo() {
  // Dinamo gerak jika Servo Diam, Dinamo Diam, dan Jeda Aman lewat
  if (!isServoMoving && !isDinamoMoving) {
    if (millis() - servoStopTimestamp > delayStabilisasi) {
      
      // Jika Servo BUKA (90) & Jemuran DI DALAM -> KELUARKAN
      if (currentServoPosition >= (POSISI_TERBUKA - 5) && !posisiJemuranDiLuar) {
        Serial.println(">> LOGIKA: Servo Terbuka -> Keluarkan Jemuran!");
        gerakDinamoKeluar();
        posisiJemuranDiLuar = true; 
        preferences.putBool("posisiJemuranDiLuar", true);
      }
      
      // Jika Servo TUTUP (0) & Jemuran DI LUAR -> MASUKKAN
      else if (currentServoPosition <= (POSISI_TERTUTUP + 5) && posisiJemuranDiLuar) {
        Serial.println(">> LOGIKA: Servo Tertutup -> Masukkan Jemuran!");
        gerakDinamoMasuk();
        posisiJemuranDiLuar = false; 
        preferences.putBool("posisiJemuranDiLuar", false);
      }
    }
  }
}

// ==========================================
//          KONTROL DINAMO
// ==========================================

void gerakDinamoKeluar() {
  digitalWrite(pinDinamo1, LOW); 
  digitalWrite(pinDinamo2, LOW); 
  delay(100);
  
  digitalWrite(pinDinamo1, HIGH);
  digitalWrite(pinDinamo2, LOW);
  
  dinamoStatus = "MAJU"; 
  isDinamoMoving = true; 
  dinamoStartTime = millis();
  Serial.println(">> DINAMO MAJU");
}

void gerakDinamoMasuk() {
  digitalWrite(pinDinamo1, LOW); 
  digitalWrite(pinDinamo2, LOW); 
  delay(100);
  
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, HIGH);
  
  dinamoStatus = "MUNDUR"; 
  isDinamoMoving = true; 
  dinamoStartTime = millis();
  Serial.println(">> DINAMO MUNDUR");
}

void stopDinamo() {
  digitalWrite(pinDinamo1, LOW); 
  digitalWrite(pinDinamo2, LOW);
  isDinamoMoving = false; 
  dinamoStatus = "STOP";
}

void updateDinamoStop() {
  if (isDinamoMoving && (millis() - dinamoStartTime >= dinamoDuration)) {
    stopDinamo();
    Serial.println(">> Dinamo Selesai (Timer).");
  }
}

// ==========================================
//          FUZZY LOGIC (16 RULES)
// ==========================================

void fuzzifikasi() {
  if (statusHujan == LOW) { uHujanYa = 1.0; uHujanTidak = 0.0; } else { uHujanYa = 0.0; uHujanTidak = 1.0; }
  if (statusCahaya == HIGH) { uCahayaGelap = 1.0; uCahayaTerang = 0.0; } else { uCahayaGelap = 0.0; uCahayaTerang = 1.0; }
  if (suhu <= 26) { uSuhuDingin = 1.0; uSuhuPanas = 0.0; } else if (suhu >= 32) { uSuhuDingin = 0.0; uSuhuPanas = 1.0; } else { uSuhuDingin = (32.0 - suhu) / 6.0; uSuhuPanas = (suhu - 26.0) / 6.0; }
  if (kelembaban <= 50) { uLembabKering = 1.0; uLembabBasah = 0.0; } else if (kelembaban >= 70) { uLembabKering = 0.0; uLembabBasah = 1.0; } else { uLembabKering = (70.0 - kelembaban) / 20.0; uLembabBasah = (kelembaban - 50.0) / 20.0; }
}

void processFuzzyLogic() {
  fuzzifikasi();
  float r[17];
  // Rule 1-8: HUJAN -> TUTUP
  r[1] = min(uHujanYa, min(uCahayaGelap, min(uSuhuDingin, uLembabBasah)));
  r[2] = min(uHujanYa, min(uCahayaGelap, min(uSuhuDingin, uLembabKering)));
  r[3] = min(uHujanYa, min(uCahayaGelap, min(uSuhuPanas, uLembabBasah)));
  r[4] = min(uHujanYa, min(uCahayaGelap, min(uSuhuPanas, uLembabKering)));
  r[5] = min(uHujanYa, min(uCahayaTerang, min(uSuhuDingin, uLembabBasah)));
  r[6] = min(uHujanYa, min(uCahayaTerang, min(uSuhuDingin, uLembabKering)));
  r[7] = min(uHujanYa, min(uCahayaTerang, min(uSuhuPanas, uLembabBasah)));
  r[8] = min(uHujanYa, min(uCahayaTerang, min(uSuhuPanas, uLembabKering)));
  // Rule 9-12: MALAM -> TUTUP
  r[9]  = min(uHujanTidak, min(uCahayaGelap, min(uSuhuDingin, uLembabBasah)));
  r[10] = min(uHujanTidak, min(uCahayaGelap, min(uSuhuDingin, uLembabKering)));
  r[11] = min(uHujanTidak, min(uCahayaGelap, min(uSuhuPanas, uLembabBasah)));
  r[12] = min(uHujanTidak, min(uCahayaGelap, min(uSuhuPanas, uLembabKering)));
  // Rule 13-16: SIANG -> BUKA
  r[13] = min(uHujanTidak, min(uCahayaTerang, min(uSuhuDingin, uLembabBasah)));
  r[14] = min(uHujanTidak, min(uCahayaTerang, min(uSuhuDingin, uLembabKering)));
  r[15] = min(uHujanTidak, min(uCahayaTerang, min(uSuhuPanas, uLembabBasah)));
  r[16] = min(uHujanTidak, min(uCahayaTerang, min(uSuhuPanas, uLembabKering)));

  float num = 0.0, den = 0.0;
  for(int i=1; i<=12; i++) { num += r[i]*0; den += r[i]; }
  for(int i=13; i<=16; i++) { num += r[i]*100; den += r[i]; }
  float zScore = (den != 0) ? (num / den) : 0;

  if (zScore > 50) {
    atapTerbuka = true;
    float maxVal = 0; int rule = 0;
    for(int i=13; i<=16; i++) { if(r[i]>maxVal){maxVal=r[i]; rule=i;} }
    switch(rule) {
      case 13: fuzzyStatusCondition="Angin-Anginkan"; break;
      case 14: fuzzyStatusCondition="Sejuk Cerah"; break;
      case 15: fuzzyStatusCondition="Terik Lembab"; break;
      case 16: fuzzyStatusCondition="Sangat Optimal"; break;
      default: fuzzyStatusCondition="Kondisi Baik"; break;
    }
  } else {
    atapTerbuka = false;
    if(statusHujan==LOW) fuzzyStatusCondition="Hujan Turun";
    else if(statusCahaya==HIGH) fuzzyStatusCondition="Sudah Malam";
    else fuzzyStatusCondition="Cuaca Buruk";
  }
}

// ==========================================
//          SENSOR & NETWORK
// ==========================================

bool readSensors() {
  bool changed = false;
  int newH = digitalRead(pinHujan);
  int newC = digitalRead(pinCahaya);
  if (newH != statusHujan || newC != statusCahaya) changed = true;
  statusHujan = newH; statusCahaya = newC;
  if (millis() - lastDHTReadTime >= intervalDHT) {
    lastDHTReadTime = millis();
    float t = dht.readTemperature(); float h = dht.readHumidity();
    if (!isnan(t) && !isnan(h)) {
      if (abs(t - suhu) > 0.5 || abs(h - kelembaban) > 2) changed = true;
      suhu = t; kelembaban = h;
    }
  }
  return changed;
}

void updateBuzzer() {
  if (statusHujan == LOW && (isServoMoving || isDinamoMoving)) {
    digitalWrite(pinBuzzer, HIGH); isBuzzerActive = true;
  } else {
    digitalWrite(pinBuzzer, LOW); isBuzzerActive = false;
  }
}

void handleNetwork(unsigned long currentMillis) {
  if (currentMillis - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) { wifiConnected = false; Serial.println("WiFi Putus..."); } 
    else { wifiConnected = true; }
  }
  if (wifiConnected && !client.connected()) {
    mqttConnected = false;
    if (currentMillis - lastMQTTReconnect >= mqttReconnectInterval) {
      lastMQTTReconnect = currentMillis;
      if (client.connect(("ESP32-" + String(random(0xffff), HEX)).c_str())) {
        mqttConnected = true; client.subscribe(mqtt_topic_command);
        if (!hasPlayedConnectionBeep) { 
          digitalWrite(pinBuzzer, HIGH); delay(100); digitalWrite(pinBuzzer, LOW); 
          hasPlayedConnectionBeep = true; 
        }
      }
    }
  } else if (wifiConnected) { client.loop(); }
}

void setup_wifi() {
  WiFi.begin(ssid, password);
  int tr=0; while(WiFi.status()!=WL_CONNECTED && tr<20){delay(500); tr++;}
  if(WiFi.status()==WL_CONNECTED) wifiConnected=true;
}

void sendSensorDataMQTT() {
  if (!mqttConnected) return;
  StaticJsonDocument<256> doc;
  doc["device_id"] = device_id;
  doc["mode"] = isAutoMode ? "auto" : "manual";
  doc["isRaining"] = (statusHujan == LOW);
  doc["isDark"] = (statusCahaya == HIGH);
  doc["temperature"] = suhu;
  doc["humidity"] = kelembaban;
  doc["fuzzy_cond"] = fuzzyStatusCondition;
  doc["servoPosition"] = currentServoPosition;
  doc["dinamoStatus"] = dinamoStatus;
  doc["isBuzzerOn"] = isBuzzerActive;
  char buf[256]; serializeJson(doc, buf);
  client.publish(mqtt_topic_data, buf, false);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = ""; for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  StaticJsonDocument<128> doc; deserializeJson(doc, msg);
  const char* cmd = doc["command"];
  if (!cmd) return;
  if (strcmp(cmd, "setMode") == 0) {
    const char* m = doc["mode"];
    if (m) isAutoMode = (strcmp(m, "auto") == 0);
  }
  else if (strcmp(cmd, "setServo") == 0 && !isAutoMode) {
    const char* act = doc["action"];
    if (strcmp(act, "open") == 0) updateTargetServo(POSISI_TERBUKA);
    else if (strcmp(act, "close") == 0) updateTargetServo(POSISI_TERTUTUP);
  }
  else if (strcmp(cmd, "setDinamo") == 0 && !isAutoMode) {
    const char* act = doc["action"];
    if (strcmp(act, "maju") == 0) gerakDinamoKeluar();
    else if (strcmp(act, "mundur") == 0) gerakDinamoMasuk();
    else if (strcmp(act, "stop") == 0) stopDinamo();
  }
}

// === FUNGSI CETAK STATUS LENGKAP ===
void printFullStatus() {
  Serial.println("\n--------------------------------");
  Serial.print("MODE: "); Serial.println(isAutoMode ? "AUTO (Fuzzy)" : "MANUAL");
  
  Serial.print("[SENSOR] ");
  Serial.print("Hujan: "); Serial.print(statusHujan == LOW ? "YA" : "TIDAK");
  Serial.print(" | Cahaya: "); Serial.print(statusCahaya == HIGH ? "GELAP" : "TERANG");
  Serial.print(" | Temp: "); Serial.print(suhu, 1);
  Serial.print(" C | Hum: "); Serial.print(kelembaban, 0); Serial.println(" %");
  
  Serial.print("[FUZZY] "); Serial.println(fuzzyStatusCondition);
  
  Serial.print("[MOTOR] Servo: "); Serial.print(currentServoPosition);
  Serial.print(" | Target: "); Serial.print(targetServoPosition);
  Serial.print(" | Dinamo: "); Serial.println(dinamoStatus);
  
  Serial.print("[LOGIC] Jemuran Fisik: "); Serial.println(posisiJemuranDiLuar ? "DI LUAR" : "DI DALAM");
  
  Serial.print("[NET] WiFi: "); Serial.print(wifiConnected ? "ON" : "OFF");
  Serial.print(" | MQTT: "); Serial.println(mqttConnected ? "ON" : "OFF");
  Serial.println("--------------------------------");
}