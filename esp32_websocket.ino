#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <DHT.h>

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
const int pinHujan = 25;    // Sensor Hujan (LOW = Hujan)
const int pinCahaya = 26;   // Sensor Cahaya (HIGH = Gelap)
const int pinDHT = 4;       // Pin DHT11
const int pinBuzzer = 14;   // Pin Buzzer

// --- PIN DINAMO (L298N) ---
const int pinDinamo1 = 27;  // Masuk ke IN1 L298N
const int pinDinamo2 = 12;  // Masuk ke IN2 L298N

// ===== DHT Configuration =====
#define DHTTYPE DHT11
DHT dht(pinDHT, DHTTYPE);

// ===== Servo Object & Posisi =====
Servo atapServo;
// MODIFIKASI: Mengubah batas maksimal menjadi 90 derajat
const int POSISI_TERTUTUP = 0;   // 0 Derajat (Tutup)
const int POSISI_TERBUKA = 90;   // 90 Derajat (Buka Maksimal)

// ===== Variables =====
int currentServoPosition = 0;
int targetServoPosition = 0;
bool isAutoMode = true;
bool atapTerbuka = false;
bool isBuzzerActive = false; 

// --- VARIABLES DINAMO ---
unsigned long dinamoStartTime = 0;
bool isDinamoMoving = false;
const long dinamoDuration = 5000; // Durasi dinamo berputar (5 Detik)
String dinamoStatus = "STOP";     // Status: MAJU, MUNDUR, STOP

// Non-blocking servo movement
bool isServoMoving = false;
unsigned long lastServoMoveTime = 0;
const int SERVO_STEP = 2;
const int SERVO_DELAY = 15;

// Sensor readings
int statusHujan = HIGH;    // HIGH = Tidak Hujan, LOW = Hujan
int statusCahaya = LOW;    // LOW = Terang, HIGH = Gelap
float suhu = 0.0;
float kelembaban = 0.0;

// === VARIABEL FUZZY LOGIC ===
float uSuhuDingin = 0.0;
float uSuhuPanas = 0.0;
float uLembabKering = 0.0;
float uLembabBasah = 0.0;

// Status strings
String fuzzyStatusCondition = "Menunggu..."; 
String currentRoofStatus = "TERTUTUP"; 

// Counter & Timing
int counter = 0;
unsigned long previousMillis = 0;
const long interval = 500;  
unsigned long lastDHTReadTime = 0;
const long intervalDHT = 2000;

// Change detection
int prevStatusHujan = HIGH;
int prevStatusCahaya = LOW;
bool sensorChanged = false;

void setup() {
  Serial.begin(115200);
  delay(1000); // Jeda aman boot

  Serial.println("\n======================================");
  Serial.println(" JEMURAN LENGKAP (SERVO 90 + DINAMO) ");
  Serial.println("======================================");

  pinMode(pinHujan, INPUT);
  pinMode(pinCahaya, INPUT);
  pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinBuzzer, LOW); 

  pinMode(pinDinamo1, OUTPUT);
  pinMode(pinDinamo2, OUTPUT);
  stopDinamo(); 

  dht.begin();

  // ESP32PWM::allocateTimer(0); // Optional tergantung library version

  atapServo.setPeriodHertz(50);
  atapServo.attach(pinServo);
  
  // Set posisi awal tertutup (0)
  atapServo.write(POSISI_TERTUTUP);
  currentServoPosition = POSISI_TERTUTUP;
  targetServoPosition = POSISI_TERTUTUP;
  
  setup_wifi();
  
  client.setBufferSize(1024);
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
}

void loop() {
  if (!client.connected()) {
    reconnect();
  }
  client.loop();

  // Update pergerakan motor (Non-blocking)
  updateServoMovement();
  updateDinamoStop(); 
  
  // Baca Sensor
  readSensors();
  
  unsigned long currentMillis = millis();
  
  // Update Logic Utama
  if (currentMillis - previousMillis >= interval || sensorChanged || isServoMoving || isDinamoMoving) {
    previousMillis = currentMillis;
    sensorChanged = false;
    
    // 1. Jalankan Logika Fuzzy (Otomatisasi)
    processFuzzyLogic(); 
    
    // 2. KONTROL BUZZER (MODIFIKASI UTAMA)
    // Syarat Buzzer Bunyi: Ada Hujan DAN (Servo Sedang Gerak ATAU Dinamo Sedang Gerak)
    // Jika motor berhenti, buzzer mati.
    if (statusHujan == LOW && (isServoMoving || isDinamoMoving)) {
      digitalWrite(pinBuzzer, HIGH);
      isBuzzerActive = true;
    } else {
      digitalWrite(pinBuzzer, LOW);
      isBuzzerActive = false;
    }
    
    printToSerial();
    sendSensorDataMQTT();
  }
  
  // Mencegah WDT Reset
  delay(1); 
}

// ==========================================
//          KONTROL DINAMO (L298N)
// ==========================================

void gerakDinamoKeluar() {
  if (dinamoStatus != "MAJU") { 
    Serial.println(">>> DINAMO: Bergerak MAJU (Keluar)...");
    digitalWrite(pinDinamo1, HIGH);
    digitalWrite(pinDinamo2, LOW);
    
    dinamoStatus = "MAJU";
    isDinamoMoving = true;
    dinamoStartTime = millis(); 
  }
}

void gerakDinamoMasuk() {
  if (dinamoStatus != "MUNDUR") { 
    Serial.println(">>> DINAMO: Bergerak MUNDUR (Masuk)...");
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, HIGH);
    
    dinamoStatus = "MUNDUR";
    isDinamoMoving = true;
    dinamoStartTime = millis(); 
  }
}

void stopDinamo() {
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, LOW);
  isDinamoMoving = false;
  dinamoStatus = "STOP"; 
}

void updateDinamoStop() {
  if (isDinamoMoving) {
    // Cek apakah durasi sudah habis
    if (millis() - dinamoStartTime >= dinamoDuration) {
      stopDinamo();
      Serial.println(">>> DINAMO: Berhenti (Timer Selesai).");
    }
  }
}

// ==========================================
//          BAGIAN UTAMA LOGIKA FUZZY
// ==========================================

void fuzzifikasi() {
  // Fuzzifikasi SUHU
  if (suhu <= 26) {
    uSuhuDingin = 1.0; uSuhuPanas = 0.0;
  } else if (suhu >= 32) {
    uSuhuDingin = 0.0; uSuhuPanas = 1.0;
  } else {
    uSuhuDingin = (32.0 - suhu) / (32.0 - 26.0);
    uSuhuPanas = (suhu - 26.0) / (32.0 - 26.0);
  }

  // Fuzzifikasi KELEMBABAN
  if (kelembaban <= 50) {
    uLembabKering = 1.0; uLembabBasah = 0.0;
  } else if (kelembaban >= 70) {
    uLembabKering = 0.0; uLembabBasah = 1.0;
  } else {
    uLembabKering = (70.0 - kelembaban) / (70.0 - 50.0);
    uLembabBasah = (kelembaban - 50.0) / (70.0 - 50.0);
  }
}

void processFuzzyLogic() {
  fuzzifikasi();

  int keputusan = POSISI_TERTUTUP; 
  String keterangan = "Menunggu...";

  // Rule Base Prioritas (Hard Rules)
  if (statusHujan == LOW) { 
    keputusan = POSISI_TERTUTUP;
    keterangan = "Ada Hujan";
  }
  else if (statusCahaya == HIGH) { 
    keputusan = POSISI_TERTUTUP;
    keterangan = "Sudah Malam";
  }
  else {
    // Fuzzy Inference (Tsukamoto / Sugeno Simple)
    float r13 = min(uSuhuDingin, uLembabBasah);
    float r14 = min(uSuhuDingin, uLembabKering);
    float r15 = min(uSuhuPanas, uLembabBasah);
    float r16 = min(uSuhuPanas, uLembabKering);

    float maxVal = r13;
    int ruleDominan = 13;
    if (r14 > maxVal) { maxVal = r14; ruleDominan = 14; }
    if (r15 > maxVal) { maxVal = r15; ruleDominan = 15; }
    if (r16 > maxVal) { maxVal = r16; ruleDominan = 16; }

    keputusan = POSISI_TERBUKA; // Default buka jika kondisi aman
    switch (ruleDominan) {
      case 13: keterangan = "Angin-Anginkan"; break;
      case 14: keterangan = "Sejuk Cerah"; break;
      case 15: keterangan = "Terik Lembab"; break;
      case 16: keterangan = "Sangat Optimal"; break;
    }
  }

  fuzzyStatusCondition = keterangan;

  // Eksekusi Gerakan (Serentak Servo & Dinamo)
  if (isAutoMode) {
    if (keputusan == POSISI_TERBUKA && !atapTerbuka) {
      // Panggil keduanya agar jalan serentak
      bukaAtap();          
      gerakDinamoKeluar();  
    } 
    else if (keputusan == POSISI_TERTUTUP && atapTerbuka) {
      // Panggil keduanya agar jalan serentak
      tutupAtap();
      gerakDinamoMasuk();   
    }
  }
}

// ==========================================
//        FUNGSI PENDUKUNG & SERIAL
// ==========================================

void printToSerial() {
  Serial.print("[SENSOR] ");
  Serial.print("Hujan: "); 
  Serial.print((statusHujan == LOW) ? "YA (Basah)" : "TIDAK (Kering)");
  
  Serial.print(" | Cahaya: ");
  Serial.print((statusCahaya == HIGH) ? "GELAP" : "TERANG");
  
  Serial.print(" | Temp: "); 
  Serial.print(suhu, 1);
  Serial.print("C");
  
  Serial.print(" | Hum: "); 
  Serial.print(kelembaban, 0);
  Serial.print("%");

  Serial.print(" || [STATUS] ");
  Serial.print("Dinamo: "); Serial.print(dinamoStatus); 
  Serial.print(" | Atap: "); Serial.print(currentRoofStatus);
  Serial.print(" | Buzzer: "); Serial.println(isBuzzerActive ? "ON" : "OFF");
}

void updateServoMovement() {
  if (!isServoMoving) return;
  unsigned long currentMillis = millis();
  if (currentMillis - lastServoMoveTime < SERVO_DELAY) return;
  lastServoMoveTime = currentMillis;
  
  if (currentServoPosition < targetServoPosition) {
    currentServoPosition += SERVO_STEP;
    if (currentServoPosition >= targetServoPosition) currentServoPosition = targetServoPosition;
  } else if (currentServoPosition > targetServoPosition) {
    currentServoPosition -= SERVO_STEP;
    if (currentServoPosition <= targetServoPosition) currentServoPosition = targetServoPosition;
  } 
  
  // Jika target tercapai, set moving false
  if (currentServoPosition == targetServoPosition) {
    isServoMoving = false;
  }
  
  atapServo.write(currentServoPosition);
  
  // Update status string
  // Karena batas atas 90, kita anggap terbuka jika > 10 derajat
  atapTerbuka = (currentServoPosition > 10);
  currentRoofStatus = atapTerbuka ? "TERBUKA" : "TERTUTUP";
}

void readSensors() {
  unsigned long currentMillis = millis();
  int newH = digitalRead(pinHujan);
  int newC = digitalRead(pinCahaya);
  
  if (newH != prevStatusHujan || newC != prevStatusCahaya) {
    sensorChanged = true; prevStatusHujan = newH; prevStatusCahaya = newC;
  }
  statusHujan = newH;
  statusCahaya = newC;

  if (currentMillis - lastDHTReadTime >= intervalDHT) {
    lastDHTReadTime = currentMillis;
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    if (!isnan(t)) suhu = t;
    if (!isnan(h)) kelembaban = h;
  }
}

// Fungsi pembantu set target
void bukaAtap() { 
  targetServoPosition = POSISI_TERBUKA; // 90
  isServoMoving = true; 
}

void tutupAtap() { 
  targetServoPosition = POSISI_TERTUTUP; // 0
  isServoMoving = true; 
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = ""; for (unsigned int i = 0; i < length; i++) msg += (char)payload[i];
  
  StaticJsonDocument<200> doc; deserializeJson(doc, msg);
  
  const char* cmd = doc["command"];
  if (cmd != nullptr) {
    if (strcmp(cmd, "setMode") == 0) {
      const char* m = doc["mode"];
      if (strcmp(m, "auto") == 0) isAutoMode = true;
      else if (strcmp(m, "manual") == 0) isAutoMode = false;
      sendSensorDataMQTT();
    }
    // Manual Control
    else if (strcmp(cmd, "setServo") == 0 && !isAutoMode) {
      const char* act = doc["action"];
      if (strcmp(act, "open") == 0) { bukaAtap(); }
      else if (strcmp(act, "close") == 0) { tutupAtap(); }
      
      if (doc.containsKey("position")) {
        int pos = doc["position"];
        targetServoPosition = constrain(pos, 0, 90); // Limit manual juga 0-90
        isServoMoving = true;
      }
    }
    else if (strcmp(cmd, "setDinamo") == 0 && !isAutoMode) {
      const char* act = doc["action"];
      if (strcmp(act, "maju") == 0) { gerakDinamoKeluar(); } 
      else if (strcmp(act, "mundur") == 0) { gerakDinamoMasuk(); }
      else if (strcmp(act, "stop") == 0) { stopDinamo(); }
    }
  }
}

void setup_wifi() {
  delay(10); WiFi.begin(ssid, password);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    retry++;
    if(retry > 20) break; 
  }
}

void reconnect() {
  if (!client.connected()) {
    String id = "ESP32-" + String(random(0xffff), HEX);
    if (client.connect(id.c_str())) client.subscribe(mqtt_topic_command);
  }
}

void sendSensorDataMQTT() {
  StaticJsonDocument<512> doc;
  doc["device_id"] = device_id;
  doc["timestamp"] = millis();
  doc["mode"] = isAutoMode ? "auto" : "manual";
  doc["isRaining"] = (statusHujan == LOW);
  doc["isDark"] = (statusCahaya == HIGH);
  doc["temperature"] = suhu;
  doc["humidity"] = kelembaban;
  doc["fuzzy_cond"] = fuzzyStatusCondition;
  
  doc["isBuzzerOn"] = isBuzzerActive;
  doc["servoPosition"] = currentServoPosition;
  doc["dinamoStatus"] = dinamoStatus; 
  
  String jsonString;
  serializeJson(doc, jsonString);
  client.publish(mqtt_topic_data, jsonString.c_str(), false);
}
