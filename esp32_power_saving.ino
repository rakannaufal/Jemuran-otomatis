#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <DHT.h>

// === ANTI REBOOT UNTUK AKI 6V ===
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"
#include "esp_pm.h"
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
const long dinamoDuration = 4000;  // REDUCED: 4 detik saja
String dinamoStatus = "STOP";

// Servo movement - SUPER SLOW untuk hemat daya
bool isServoMoving = false;
unsigned long lastServoMoveTime = 0;
const int SERVO_STEP = 1;        // 1 derajat per langkah
const int SERVO_DELAY = 50;      // 50ms delay - VERY SLOW

// Sensor readings
int statusHujan = HIGH;
int statusCahaya = LOW;
float suhu = 0.0;
float kelembaban = 0.0;

// Fuzzy variables
float uSuhuDingin = 0.0, uSuhuPanas = 0.0;
float uLembabKering = 0.0, uLembabBasah = 0.0;
String fuzzyStatusCondition = "Menunggu...";
String currentRoofStatus = "TERTUTUP";

// === TIMING - DIPERLAMBAT SEMUA ===
unsigned long previousMillis = 0;
const long interval = 2000;        // Update setiap 2 detik

unsigned long lastDHTReadTime = 0;
const long intervalDHT = 5000;     // DHT setiap 5 detik

unsigned long lastMQTTPublish = 0;
const long mqttPublishInterval = 5000;  // MQTT setiap 5 detik

unsigned long lastWiFiCheck = 0;
const long wifiCheckInterval = 30000;   // WiFi check setiap 30 detik

unsigned long lastMQTTReconnect = 0;
const long mqttReconnectInterval = 10000;

unsigned long lastSerialPrint = 0;
const long serialPrintInterval = 3000;

// State
int prevStatusHujan = HIGH;
int prevStatusCahaya = LOW;
bool wifiConnected = false;
bool mqttConnected = false;

// === MOTOR SEQUENCING ===
enum MotorState { MOTOR_IDLE, SERVO_MOVING, WAIT_BEFORE_DINAMO, DINAMO_MOVING };
MotorState motorState = MOTOR_IDLE;
unsigned long motorStateStartTime = 0;
bool pendingOpen = false;
bool pendingClose = false;

void setup() {
  // === DISABLE ALL PROTECTION FOR LOW VOLTAGE ===
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  esp_task_wdt_deinit();
  
  Serial.begin(115200);
  delay(3000);  // PENTING: Tunggu voltage stabil

  Serial.println("\n==========================================");
  Serial.println("   JEMURAN - POWER SAVING MODE (6V AKI)");
  Serial.println("==========================================");

  // === REDUCE CPU FREQUENCY UNTUK HEMAT DAYA ===
  setCpuFrequencyMhz(80);  // Default 240MHz, turunkan ke 80MHz
  Serial.print("CPU Frequency: ");
  Serial.print(getCpuFrequencyMhz());
  Serial.println(" MHz");

  // Setup pins
  pinMode(pinHujan, INPUT);
  pinMode(pinCahaya, INPUT);
  pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinBuzzer, LOW);
  
  pinMode(pinDinamo1, OUTPUT);
  pinMode(pinDinamo2, OUTPUT);
  stopDinamo();

  // DHT
  dht.begin();
  delay(1000);

  // Servo - DELAYED ATTACH
  Serial.println("Menunggu voltage stabil...");
  delay(2000);
  
  atapServo.setPeriodHertz(50);
  atapServo.attach(pinServo, 500, 2400);
  delay(500);
  
  // Set posisi awal PERLAHAN
  for (int i = 90; i >= 0; i -= 2) {
    atapServo.write(i);
    delay(30);
  }
  currentServoPosition = 0;
  targetServoPosition = 0;
  
  Serial.println("Servo siap di posisi 0");
  delay(1000);

  // WiFi dengan power saving
  setup_wifi();
  
  // MQTT
  client.setBufferSize(256);  // REDUCED buffer
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  
  Serial.println("\n=== SISTEM SIAP (Power Saving Mode) ===\n");
}

void loop() {
  unsigned long currentMillis = millis();
  
  yield();  // Biarkan background tasks jalan
  
  // === WIFI CHECK (setiap 30 detik) ===
  if (currentMillis - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = currentMillis;
    if (WiFi.status() != WL_CONNECTED) {
      wifiConnected = false;
      // Jangan reconnect terus, terlalu boros daya
      Serial.println("WiFi terputus...");
    } else {
      wifiConnected = true;
    }
  }
  
  // === MQTT (non-blocking) ===
  if (wifiConnected && !client.connected()) {
    mqttConnected = false;
    if (currentMillis - lastMQTTReconnect >= mqttReconnectInterval) {
      lastMQTTReconnect = currentMillis;
      reconnectMQTT();
    }
  } else if (wifiConnected) {
    mqttConnected = true;
    client.loop();
  }

  // === UPDATE MOTOR STATE MACHINE ===
  updateMotorStateMachine();
  
  // === BACA SENSOR ===
  readSensors();
  
  // === LOGIC UTAMA (setiap 2 detik) ===
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    processFuzzyLogic();
    updateBuzzer();
  }
  
  // === SERIAL (setiap 3 detik) ===
  if (currentMillis - lastSerialPrint >= serialPrintInterval) {
    lastSerialPrint = currentMillis;
    printStatus();
  }
  
  // === MQTT PUBLISH (setiap 5 detik) ===
  if (mqttConnected && (currentMillis - lastMQTTPublish >= mqttPublishInterval)) {
    lastMQTTPublish = currentMillis;
    sendSensorDataMQTT();
  }
  
  // === DELAY BESAR untuk hemat daya ===
  delay(50);
}

// ==========================================
//     MOTOR STATE MACHINE (SEQUENTIAL)
// ==========================================

void updateMotorStateMachine() {
  unsigned long currentMillis = millis();
  
  switch (motorState) {
    case MOTOR_IDLE:
      // Cek apakah ada pending action
      if (pendingOpen) {
        pendingOpen = false;
        Serial.println("\n>> MULAI BUKA: Servo dulu...");
        targetServoPosition = POSISI_TERBUKA;
        isServoMoving = true;
        motorState = SERVO_MOVING;
        motorStateStartTime = currentMillis;
      }
      else if (pendingClose) {
        pendingClose = false;
        Serial.println("\n>> MULAI TUTUP: Servo dulu...");
        targetServoPosition = POSISI_TERTUTUP;
        isServoMoving = true;
        motorState = SERVO_MOVING;
        motorStateStartTime = currentMillis;
      }
      break;
      
    case SERVO_MOVING:
      updateServoMovement();
      if (!isServoMoving) {
        Serial.println(">> Servo selesai, menunggu 1 detik...");
        motorState = WAIT_BEFORE_DINAMO;
        motorStateStartTime = currentMillis;
      }
      break;
      
    case WAIT_BEFORE_DINAMO:
      // Tunggu 1 detik sebelum jalankan dinamo
      if (currentMillis - motorStateStartTime >= 1000) {
        if (atapTerbuka) {
          Serial.println(">> Menjalankan Dinamo KELUAR...");
          gerakDinamoKeluar();
        } else {
          Serial.println(">> Menjalankan Dinamo MASUK...");
          gerakDinamoMasuk();
        }
        motorState = DINAMO_MOVING;
        motorStateStartTime = currentMillis;
      }
      break;
      
    case DINAMO_MOVING:
      updateDinamoStop();
      if (!isDinamoMoving) {
        Serial.println(">> Semua motor selesai.\n");
        motorState = MOTOR_IDLE;
      }
      break;
  }
}

void updateServoMovement() {
  if (!isServoMoving) return;
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastServoMoveTime < SERVO_DELAY) return;
  lastServoMoveTime = currentMillis;
  
  if (currentServoPosition < targetServoPosition) {
    currentServoPosition += SERVO_STEP;
    if (currentServoPosition >= targetServoPosition) 
      currentServoPosition = targetServoPosition;
  } else if (currentServoPosition > targetServoPosition) {
    currentServoPosition -= SERVO_STEP;
    if (currentServoPosition <= targetServoPosition) 
      currentServoPosition = targetServoPosition;
  }
  
  atapServo.write(currentServoPosition);
  
  if (currentServoPosition == targetServoPosition) {
    isServoMoving = false;
    delay(100);  // Stabilkan setelah servo stop
  }
  
  atapTerbuka = (currentServoPosition > 10);
  currentRoofStatus = atapTerbuka ? "TERBUKA" : "TERTUTUP";
}

// ==========================================
//          KONTROL DINAMO
// ==========================================

void gerakDinamoKeluar() {
  if (dinamoStatus != "MAJU") {
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, LOW);
    delay(100);
    
    digitalWrite(pinDinamo1, HIGH);
    digitalWrite(pinDinamo2, LOW);
    
    dinamoStatus = "MAJU";
    isDinamoMoving = true;
    dinamoStartTime = millis();
    Serial.println("   DINAMO: MAJU");
  }
}

void gerakDinamoMasuk() {
  if (dinamoStatus != "MUNDUR") {
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, LOW);
    delay(100);
    
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, HIGH);
    
    dinamoStatus = "MUNDUR";
    isDinamoMoving = true;
    dinamoStartTime = millis();
    Serial.println("   DINAMO: MUNDUR");
  }
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
    Serial.println("   DINAMO: STOP (selesai)");
  }
}

// ==========================================
//          SENSOR & BUZZER
// ==========================================

void readSensors() {
  statusHujan = digitalRead(pinHujan);
  statusCahaya = digitalRead(pinCahaya);

  unsigned long currentMillis = millis();
  if (currentMillis - lastDHTReadTime >= intervalDHT) {
    lastDHTReadTime = currentMillis;
    
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    if (!isnan(t) && t > -40 && t < 80) suhu = t;
    if (!isnan(h) && h >= 0 && h <= 100) kelembaban = h;
  }
}

void updateBuzzer() {
  if (statusHujan == LOW && (isServoMoving || isDinamoMoving)) {
    digitalWrite(pinBuzzer, HIGH);
    isBuzzerActive = true;
  } else {
    digitalWrite(pinBuzzer, LOW);
    isBuzzerActive = false;
  }
}

// ==========================================
//          FUZZY LOGIC
// ==========================================

void fuzzifikasi() {
  if (suhu <= 26) { uSuhuDingin = 1.0; uSuhuPanas = 0.0; }
  else if (suhu >= 32) { uSuhuDingin = 0.0; uSuhuPanas = 1.0; }
  else { uSuhuDingin = (32.0 - suhu) / 6.0; uSuhuPanas = (suhu - 26.0) / 6.0; }

  if (kelembaban <= 50) { uLembabKering = 1.0; uLembabBasah = 0.0; }
  else if (kelembaban >= 70) { uLembabKering = 0.0; uLembabBasah = 1.0; }
  else { uLembabKering = (70.0 - kelembaban) / 20.0; uLembabBasah = (kelembaban - 50.0) / 20.0; }
}

void processFuzzyLogic() {
  fuzzifikasi();

  int keputusan = POSISI_TERTUTUP;
  String keterangan = "Menunggu...";

  if (statusHujan == LOW) {
    keputusan = POSISI_TERTUTUP;
    keterangan = "Ada Hujan";
  }
  else if (statusCahaya == HIGH) {
    keputusan = POSISI_TERTUTUP;
    keterangan = "Sudah Malam";
  }
  else {
    float r13 = min(uSuhuDingin, uLembabBasah);
    float r14 = min(uSuhuDingin, uLembabKering);
    float r15 = min(uSuhuPanas, uLembabBasah);
    float r16 = min(uSuhuPanas, uLembabKering);

    float maxVal = r13;
    int rule = 13;
    if (r14 > maxVal) { maxVal = r14; rule = 14; }
    if (r15 > maxVal) { maxVal = r15; rule = 15; }
    if (r16 > maxVal) { maxVal = r16; rule = 16; }

    keputusan = POSISI_TERBUKA;
    switch (rule) {
      case 13: keterangan = "Angin-Anginkan"; break;
      case 14: keterangan = "Sejuk Cerah"; break;
      case 15: keterangan = "Terik Lembab"; break;
      case 16: keterangan = "Sangat Optimal"; break;
    }
  }

  fuzzyStatusCondition = keterangan;

  // === EKSEKUSI (hanya jika MOTOR_IDLE) ===
  if (isAutoMode && motorState == MOTOR_IDLE) {
    if (keputusan == POSISI_TERBUKA && !atapTerbuka) {
      Serial.println("\n========================================");
      Serial.println("  KEPUTUSAN: BUKA JEMURAN");
      Serial.println("========================================");
      pendingOpen = true;
    }
    else if (keputusan == POSISI_TERTUTUP && atapTerbuka) {
      Serial.println("\n========================================");
      Serial.println("  KEPUTUSAN: TUTUP JEMURAN");  
      Serial.println("========================================");
      pendingClose = true;
    }
  }
}

// ==========================================
//          WIFI & MQTT
// ==========================================

void setup_wifi() {
  Serial.println("Menghubungkan WiFi...");
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(500);
  
  // PENTING: Set WiFi power saving mode
  esp_wifi_set_ps(WIFI_PS_MAX_MODEM);
  
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 20) {
    delay(500);
    Serial.print(".");
    retry++;
    yield();
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
  } else {
    Serial.println("\nWiFi gagal (akan coba lagi nanti)");
  }
}

void reconnectMQTT() {
  String id = "ESP32-" + String(random(0xffff), HEX);
  if (client.connect(id.c_str())) {
    mqttConnected = true;
    client.subscribe(mqtt_topic_command);
    Serial.println("MQTT OK");
  }
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
  
  char buf[256];
  serializeJson(doc, buf);
  client.publish(mqtt_topic_data, buf, false);
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  
  StaticJsonDocument<128> doc;
  if (deserializeJson(doc, msg)) return;
  
  const char* cmd = doc["command"];
  if (!cmd) return;
  
  if (strcmp(cmd, "setMode") == 0) {
    const char* m = doc["mode"];
    if (m) {
      isAutoMode = (strcmp(m, "auto") == 0);
      Serial.print("Mode: "); Serial.println(isAutoMode ? "AUTO" : "MANUAL");
    }
  }
  else if (strcmp(cmd, "setServo") == 0 && !isAutoMode && motorState == MOTOR_IDLE) {
    const char* act = doc["action"];
    if (act) {
      if (strcmp(act, "open") == 0) pendingOpen = true;
      else if (strcmp(act, "close") == 0) pendingClose = true;
    }
  }
  else if (strcmp(cmd, "setDinamo") == 0 && !isAutoMode && motorState == MOTOR_IDLE) {
    const char* act = doc["action"];
    if (act) {
      if (strcmp(act, "maju") == 0) gerakDinamoKeluar();
      else if (strcmp(act, "mundur") == 0) gerakDinamoMasuk();
      else if (strcmp(act, "stop") == 0) stopDinamo();
    }
  }
}

// ==========================================
//          STATUS PRINT
// ==========================================

void printStatus() {
  Serial.print("[SENSOR] H:"); 
  Serial.print(statusHujan == LOW ? "Y" : "N");
  Serial.print(" C:"); Serial.print(statusCahaya == HIGH ? "G" : "T");
  Serial.print(" T:"); Serial.print(suhu, 0);
  Serial.print(" H:"); Serial.print(kelembaban, 0);
  
  Serial.print(" [MOTOR] S:"); Serial.print(currentServoPosition);
  Serial.print(" D:"); Serial.print(dinamoStatus);
  Serial.print(" St:"); Serial.print(motorState);
  
  Serial.print(" [NET] W:"); Serial.print(wifiConnected ? "+" : "-");
  Serial.print(" M:"); Serial.println(mqttConnected ? "+" : "-");
}
