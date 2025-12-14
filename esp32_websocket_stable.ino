#include <WiFi.h>
#include <PubSubClient.h>
#include <ArduinoJson.h>
#include <ESP32Servo.h>
#include <DHT.h>

// === ANTI REBOOT (BROWNOUT + WATCHDOG) ===
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#include "esp_task_wdt.h"

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
const int POSISI_TERTUTUP = 0;   // 0 Derajat (Tutup)
const int POSISI_TERBUKA = 90;   // 90 Derajat (Buka)

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
const int SERVO_STEP = 1;       // DIPERLAMBAT: Langkah kecil untuk hemat daya
const int SERVO_DELAY = 30;     // DIPERLAMBAT: Delay lebih lama antar langkah (ms)

// Sensor readings
int statusHujan = HIGH;    // HIGH = Tidak Hujan, LOW = Hujan
int statusCahaya = LOW;    // LOW = Terang, HIGH = Gelap
float suhu = 0.0;
float kelembaban = 0.0;

// === VARIABEL FUZZY LOGIC ===
float uSuhuDingin = 0.0; float uSuhuPanas = 0.0;
float uLembabKering = 0.0; float uLembabBasah = 0.0;

// Status strings
String fuzzyStatusCondition = "Menunggu..."; 
String currentRoofStatus = "TERTUTUP"; 

// === TIMING VARIABLES (STABILITY) ===
unsigned long previousMillis = 0;
const long interval = 1000;  // INCREASED: Update setiap 1 detik, bukan 500ms

unsigned long lastDHTReadTime = 0;
const long intervalDHT = 3000;  // INCREASED: Baca DHT setiap 3 detik

unsigned long lastMQTTPublish = 0;
const long mqttPublishInterval = 2000;  // Publish MQTT setiap 2 detik

unsigned long lastWiFiCheck = 0;
const long wifiCheckInterval = 10000;  // Cek WiFi setiap 10 detik

unsigned long lastMQTTReconnectAttempt = 0;
const long mqttReconnectInterval = 5000;  // Reconnect MQTT setiap 5 detik (jika terputus)

unsigned long lastSerialPrint = 0;
const long serialPrintInterval = 2000;  // Print ke Serial setiap 2 detik

// Change detection
int prevStatusHujan = HIGH;
int prevStatusCahaya = LOW;
bool sensorChanged = false;

// === STABILITY FLAGS ===
bool wifiConnected = false;
bool mqttConnected = false;
int wifiRetryCount = 0;
int mqttRetryCount = 0;
const int MAX_WIFI_RETRIES = 3;
const int MAX_MQTT_RETRIES = 3;

void setup() {
  // === MATIKAN DETEKSI BROWNOUT AGAR TIDAK REBOOT ===
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0); 
  
  // === DISABLE WATCHDOG TIMER (Prevent reboot from long operations) ===
  esp_task_wdt_deinit();
  
  Serial.begin(115200);
  delay(2000);  // INCREASED: Beri waktu lebih untuk stabilisasi

  Serial.println("\n==========================================");
  Serial.println(" JEMURAN OTOMATIS - STABLE VERSION ");
  Serial.println("==========================================");

  // === SETUP PINS ===
  pinMode(pinHujan, INPUT);
  pinMode(pinCahaya, INPUT);
  pinMode(pinBuzzer, OUTPUT);
  digitalWrite(pinBuzzer, LOW); 

  pinMode(pinDinamo1, OUTPUT);
  pinMode(pinDinamo2, OUTPUT);
  stopDinamo(); 

  // === SETUP DHT ===
  dht.begin();
  delay(500);  // Beri waktu DHT untuk siap

  // === SETUP SERVO (dengan delay) ===
  Serial.println("Menginisialisasi Servo...");
  atapServo.setPeriodHertz(50);
  delay(100);
  atapServo.attach(pinServo, 500, 2400);  // Min/Max pulse width untuk stabilitas
  delay(200);
  
  // Posisi Awal: Tertutup (0)
  atapServo.write(POSISI_TERTUTUP);
  currentServoPosition = POSISI_TERTUTUP;
  targetServoPosition = POSISI_TERTUTUP;
  delay(500);  // Beri waktu servo untuk bergerak
  
  Serial.println("Servo siap!");
  
  // === SETUP WIFI ===
  setup_wifi();
  
  // === SETUP MQTT ===
  client.setBufferSize(512);  // REDUCED buffer size
  client.setServer(mqtt_server, 1883);
  client.setCallback(mqttCallback);
  client.setKeepAlive(60);  // Keep alive 60 detik
  
  Serial.println("Setup selesai! Memulai loop utama...\n");
}

void loop() {
  unsigned long currentMillis = millis();
  
  // === WATCHDOG FEED (mencegah timeout) ===
  yield();  // Biarkan sistem lain berjalan
  
  // === CEK WIFI (setiap 10 detik) ===
  if (currentMillis - lastWiFiCheck >= wifiCheckInterval) {
    lastWiFiCheck = currentMillis;
    checkWiFiConnection();
  }
  
  // === CEK MQTT (non-blocking) ===
  if (wifiConnected) {
    if (!client.connected()) {
      mqttConnected = false;
      // Reconnect dengan interval
      if (currentMillis - lastMQTTReconnectAttempt >= mqttReconnectInterval) {
        lastMQTTReconnectAttempt = currentMillis;
        reconnectMQTT();
      }
    } else {
      mqttConnected = true;
      mqttRetryCount = 0;
      client.loop();
    }
  }

  // === UPDATE MOTOR (Non-blocking) ===
  updateServoMovement();
  updateDinamoStop(); 
  
  // === BACA SENSOR ===
  readSensors();
  
  // === UPDATE LOGIC UTAMA (setiap 1 detik) ===
  if (currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    
    // 1. Jalankan Logika Fuzzy (Otomatisasi)
    processFuzzyLogic(); 
    
    // 2. KONTROL BUZZER
    updateBuzzer();
  }
  
  // === PRINT KE SERIAL (setiap 2 detik) ===
  if (currentMillis - lastSerialPrint >= serialPrintInterval) {
    lastSerialPrint = currentMillis;
    printToSerial();
  }
  
  // === PUBLISH MQTT (setiap 2 detik) ===
  if (mqttConnected && (currentMillis - lastMQTTPublish >= mqttPublishInterval)) {
    lastMQTTPublish = currentMillis;
    sendSensorDataMQTT();
  }
  
  // === DELAY UNTUK STABILITAS ===
  delay(10);  // INCREASED: 10ms delay untuk stabilitas CPU
}

// ==========================================
//          WIFI FUNCTIONS (STABLE)
// ==========================================

void setup_wifi() {
  Serial.println("Menghubungkan ke WiFi...");
  Serial.print("SSID: ");
  Serial.println(ssid);
  
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  
  WiFi.begin(ssid, password);
  
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 30) {  // Max 15 detik
    delay(500);
    Serial.print(".");
    retry++;
    yield();  // Feed watchdog
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    wifiConnected = true;
    wifiRetryCount = 0;
    Serial.println("\nWiFi terhubung!");
    Serial.print("IP Address: ");
    Serial.println(WiFi.localIP());
  } else {
    wifiConnected = false;
    Serial.println("\nWiFi gagal terhubung! Akan dicoba lagi...");
  }
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    wifiConnected = false;
    wifiRetryCount++;
    
    Serial.print("WiFi terputus! Mencoba reconnect (");
    Serial.print(wifiRetryCount);
    Serial.print("/");
    Serial.print(MAX_WIFI_RETRIES);
    Serial.println(")...");
    
    if (wifiRetryCount <= MAX_WIFI_RETRIES) {
      WiFi.disconnect();
      delay(1000);
      WiFi.begin(ssid, password);
      delay(5000);  // Tunggu 5 detik untuk koneksi
      
      if (WiFi.status() == WL_CONNECTED) {
        wifiConnected = true;
        wifiRetryCount = 0;
        Serial.println("WiFi reconnected!");
      }
    } else {
      Serial.println("WiFi retry limit reached. Akan dicoba lagi nanti...");
      wifiRetryCount = 0;  // Reset untuk mencoba lagi nanti
    }
  } else {
    wifiConnected = true;
    wifiRetryCount = 0;
  }
}

// ==========================================
//          MQTT FUNCTIONS (STABLE)
// ==========================================

void reconnectMQTT() {
  if (mqttRetryCount >= MAX_MQTT_RETRIES) {
    Serial.println("MQTT retry limit reached. Akan dicoba lagi nanti...");
    mqttRetryCount = 0;
    return;
  }
  
  mqttRetryCount++;
  Serial.print("Menghubungkan ke MQTT (");
  Serial.print(mqttRetryCount);
  Serial.print("/");
  Serial.print(MAX_MQTT_RETRIES);
  Serial.println(")...");
  
  String clientId = "ESP32-" + String(random(0xffff), HEX);
  
  if (client.connect(clientId.c_str())) {
    mqttConnected = true;
    mqttRetryCount = 0;
    Serial.println("MQTT terhubung!");
    client.subscribe(mqtt_topic_command);
  } else {
    mqttConnected = false;
    Serial.print("MQTT gagal, rc=");
    Serial.println(client.state());
  }
}

void sendSensorDataMQTT() {
  if (!mqttConnected) return;
  
  StaticJsonDocument<384> doc;  // REDUCED size
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
  
  char jsonBuffer[384];
  serializeJson(doc, jsonBuffer);
  
  if (client.publish(mqtt_topic_data, jsonBuffer, false)) {
    // Success - silent
  } else {
    Serial.println("MQTT publish failed!");
  }
}

void mqttCallback(char* topic, byte* payload, unsigned int length) {
  String msg = "";
  for (unsigned int i = 0; i < length; i++) {
    msg += (char)payload[i];
  }
  
  Serial.print("MQTT Command: ");
  Serial.println(msg);
  
  StaticJsonDocument<256> doc;
  DeserializationError error = deserializeJson(doc, msg);
  
  if (error) {
    Serial.print("JSON parse error: ");
    Serial.println(error.c_str());
    return;
  }
  
  const char* cmd = doc["command"];
  if (cmd == nullptr) return;
  
  if (strcmp(cmd, "setMode") == 0) {
    const char* m = doc["mode"];
    if (m != nullptr) {
      if (strcmp(m, "auto") == 0) isAutoMode = true;
      else if (strcmp(m, "manual") == 0) isAutoMode = false;
      Serial.print("Mode changed to: ");
      Serial.println(isAutoMode ? "AUTO" : "MANUAL");
    }
  }
  else if (strcmp(cmd, "setServo") == 0 && !isAutoMode) {
    const char* act = doc["action"];
    if (act != nullptr) {
      if (strcmp(act, "open") == 0) bukaAtap();
      else if (strcmp(act, "close") == 0) tutupAtap();
    }
    if (doc.containsKey("position")) {
      int pos = doc["position"];
      targetServoPosition = constrain(pos, 0, 90);
      isServoMoving = true;
    }
  }
  else if (strcmp(cmd, "setDinamo") == 0 && !isAutoMode) {
    const char* act = doc["action"];
    if (act != nullptr) {
      if (strcmp(act, "maju") == 0) gerakDinamoKeluar();
      else if (strcmp(act, "mundur") == 0) gerakDinamoMasuk();
      else if (strcmp(act, "stop") == 0) stopDinamo();
    }
  }
}

// ==========================================
//          KONTROL DINAMO (L298N)
// ==========================================

void gerakDinamoKeluar() {
  // Langsung eksekusi tanpa delay check
  if (dinamoStatus != "MAJU") { 
    Serial.println(">>> DINAMO: Bergerak MAJU (Keluar)...");
    
    // Pastikan stop dulu sebelum ganti arah
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, LOW);
    delay(50);  // Brief pause before changing direction
    
    // Jalankan maju
    digitalWrite(pinDinamo1, HIGH);
    digitalWrite(pinDinamo2, LOW);
    
    dinamoStatus = "MAJU";
    isDinamoMoving = true;
    dinamoStartTime = millis();
    
    Serial.println(">>> DINAMO: Status = MAJU, Pin1=HIGH, Pin2=LOW");
  }
}

void gerakDinamoMasuk() {
  // Langsung eksekusi tanpa delay check
  if (dinamoStatus != "MUNDUR") { 
    Serial.println(">>> DINAMO: Bergerak MUNDUR (Masuk)...");
    
    // Pastikan stop dulu sebelum ganti arah
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, LOW);
    delay(50);  // Brief pause before changing direction
    
    // Jalankan mundur
    digitalWrite(pinDinamo1, LOW);
    digitalWrite(pinDinamo2, HIGH);
    
    dinamoStatus = "MUNDUR";
    isDinamoMoving = true;
    dinamoStartTime = millis();
    
    Serial.println(">>> DINAMO: Status = MUNDUR, Pin1=LOW, Pin2=HIGH");
  }
}

void stopDinamo() {
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, LOW);
  isDinamoMoving = false;
  dinamoStatus = "STOP";
  Serial.println(">>> DINAMO: STOP");
}

void updateDinamoStop() {
  if (isDinamoMoving) {
    if (millis() - dinamoStartTime >= dinamoDuration) {
      stopDinamo();
      Serial.println(">>> DINAMO: Berhenti otomatis (5 detik).");
    }
  }
}

// Fungsi test dinamo manual (panggil dari Serial Monitor)
void testDinamo() {
  Serial.println("=== TEST DINAMO ===");
  
  Serial.println("Test MAJU 2 detik...");
  digitalWrite(pinDinamo1, HIGH);
  digitalWrite(pinDinamo2, LOW);
  delay(2000);
  
  Serial.println("STOP...");
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, LOW);
  delay(1000);
  
  Serial.println("Test MUNDUR 2 detik...");
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, HIGH);
  delay(2000);
  
  Serial.println("STOP...");
  digitalWrite(pinDinamo1, LOW);
  digitalWrite(pinDinamo2, LOW);
  
  Serial.println("=== TEST SELESAI ===");
}

// ==========================================
//          SERVO FUNCTIONS
// ==========================================

void bukaAtap() { 
  Serial.println(">>> SERVO: Target BUKA (90 derajat)");
  targetServoPosition = POSISI_TERBUKA;
  isServoMoving = true; 
}

void tutupAtap() { 
  Serial.println(">>> SERVO: Target TUTUP (0 derajat)");
  targetServoPosition = POSISI_TERTUTUP;
  isServoMoving = true; 
}

void updateServoMovement() {
  if (!isServoMoving) return;
  
  unsigned long currentMillis = millis();
  if (currentMillis - lastServoMoveTime < SERVO_DELAY) return;
  lastServoMoveTime = currentMillis;
  
  // Logika gerak smooth
  if (currentServoPosition < targetServoPosition) {
    currentServoPosition += SERVO_STEP;
    if (currentServoPosition >= targetServoPosition) {
      currentServoPosition = targetServoPosition;
    }
  } else if (currentServoPosition > targetServoPosition) {
    currentServoPosition -= SERVO_STEP;
    if (currentServoPosition <= targetServoPosition) {
      currentServoPosition = targetServoPosition;
    }
  } 
  
  // Tulis posisi ke Servo
  atapServo.write(currentServoPosition);
  
  // Cek apakah sudah sampai tujuan
  if (currentServoPosition == targetServoPosition) {
    isServoMoving = false;
  }
  
  // Update status string
  atapTerbuka = (currentServoPosition > 10);
  currentRoofStatus = atapTerbuka ? "TERBUKA" : "TERTUTUP";
}

// ==========================================
//          SENSOR & BUZZER
// ==========================================

void readSensors() {
  unsigned long currentMillis = millis();
  
  // Baca sensor digital
  int newH = digitalRead(pinHujan);
  int newC = digitalRead(pinCahaya);
  
  if (newH != prevStatusHujan || newC != prevStatusCahaya) {
    sensorChanged = true;
    prevStatusHujan = newH;
    prevStatusCahaya = newC;
  }
  statusHujan = newH;
  statusCahaya = newC;

  // Baca DHT11 (setiap 3 detik)
  if (currentMillis - lastDHTReadTime >= intervalDHT) {
    lastDHTReadTime = currentMillis;
    
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    
    // Validasi nilai DHT
    if (!isnan(t) && t > -40 && t < 80) {
      suhu = t;
    }
    if (!isnan(h) && h >= 0 && h <= 100) {
      kelembaban = h;
    }
  }
}

void updateBuzzer() {
  // Buzzer bunyi jika hujan DAN ada motor yang bergerak
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
  // Fuzzifikasi SUHU
  if (suhu <= 26) {
    uSuhuDingin = 1.0; uSuhuPanas = 0.0;
  } else if (suhu >= 32) {
    uSuhuDingin = 0.0; uSuhuPanas = 1.0;
  } else {
    uSuhuDingin = (32.0 - suhu) / 6.0;
    uSuhuPanas = (suhu - 26.0) / 6.0;
  }

  // Fuzzifikasi KELEMBABAN
  if (kelembaban <= 50) {
    uLembabKering = 1.0; uLembabBasah = 0.0;
  } else if (kelembaban >= 70) {
    uLembabKering = 0.0; uLembabBasah = 1.0;
  } else {
    uLembabKering = (70.0 - kelembaban) / 20.0;
    uLembabBasah = (kelembaban - 50.0) / 20.0;
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
    // Fuzzy Inference
    float r13 = min(uSuhuDingin, uLembabBasah);
    float r14 = min(uSuhuDingin, uLembabKering);
    float r15 = min(uSuhuPanas, uLembabBasah);
    float r16 = min(uSuhuPanas, uLembabKering);

    float maxVal = r13;
    int ruleDominan = 13;
    if (r14 > maxVal) { maxVal = r14; ruleDominan = 14; }
    if (r15 > maxVal) { maxVal = r15; ruleDominan = 15; }
    if (r16 > maxVal) { maxVal = r16; ruleDominan = 16; }

    keputusan = POSISI_TERBUKA;
    switch (ruleDominan) {
      case 13: keterangan = "Angin-Anginkan"; break;
      case 14: keterangan = "Sejuk Cerah"; break;
      case 15: keterangan = "Terik Lembab"; break;
      case 16: keterangan = "Sangat Optimal"; break;
    }
  }

  fuzzyStatusCondition = keterangan;

  // === EKSEKUSI GERAKAN (AUTO MODE) ===
  // PENTING: Servo dan Dinamo TIDAK boleh jalan bersamaan untuk hemat daya!
  if (isAutoMode) {
    // Kondisi: Perlu BUKA (keputusan = 90) dan saat ini TERTUTUP
    if (keputusan == POSISI_TERBUKA && !atapTerbuka && !isServoMoving && !isDinamoMoving) {
      Serial.println("");
      Serial.println("========================================");
      Serial.println("  ACTION: MEMBUKA JEMURAN");
      Serial.println("========================================");
      
      // HANYA jalankan SERVO dulu, dinamo akan jalan setelah servo selesai
      bukaAtap();
      Serial.println(">>> Servo bergerak ke 90 derajat...");
      Serial.println(">>> Dinamo akan menyusul setelah servo selesai.");
    }
    // Setelah servo selesai BUKA, jalankan dinamo KELUAR
    else if (atapTerbuka && !isServoMoving && dinamoStatus == "STOP" && currentServoPosition == POSISI_TERBUKA) {
      // Cek apakah dinamo perlu jalan (belum pernah jalan untuk aksi ini)
      static bool needDinamoKeluar = false;
      if (keputusan == POSISI_TERBUKA && !needDinamoKeluar) {
        needDinamoKeluar = true;
        delay(500);  // Tunggu 500ms sebelum jalankan dinamo
        gerakDinamoKeluar();
        Serial.println(">>> Dinamo MAJU (setelah servo selesai buka)");
      }
      if (keputusan != POSISI_TERBUKA) needDinamoKeluar = false; // Reset flag
    }
    
    // Kondisi: Perlu TUTUP (keputusan = 0) dan saat ini TERBUKA
    else if (keputusan == POSISI_TERTUTUP && atapTerbuka && !isServoMoving && !isDinamoMoving) {
      Serial.println("");
      Serial.println("========================================");
      Serial.println("  ACTION: MENUTUP JEMURAN");
      Serial.println("========================================");
      
      // HANYA jalankan SERVO dulu, dinamo akan jalan setelah servo selesai
      tutupAtap();
      Serial.println(">>> Servo bergerak ke 0 derajat...");
      Serial.println(">>> Dinamo akan menyusul setelah servo selesai.");
    }
    // Setelah servo selesai TUTUP, jalankan dinamo MASUK
    else if (!atapTerbuka && !isServoMoving && dinamoStatus == "STOP" && currentServoPosition == POSISI_TERTUTUP) {
      static bool needDinamoMasuk = false;
      if (keputusan == POSISI_TERTUTUP && !needDinamoMasuk) {
        needDinamoMasuk = true;
        delay(500);  // Tunggu 500ms sebelum jalankan dinamo
        gerakDinamoMasuk();
        Serial.println(">>> Dinamo MUNDUR (setelah servo selesai tutup)");
      }
      if (keputusan != POSISI_TERTUTUP) needDinamoMasuk = false; // Reset flag
    }
  }
}

// ==========================================
//          SERIAL OUTPUT
// ==========================================

void printToSerial() {
  Serial.println("------------------------------------------");
  Serial.print("[SENSOR] Hujan: "); 
  Serial.print((statusHujan == LOW) ? "YA" : "TIDAK");
  Serial.print(" | Cahaya: ");
  Serial.print((statusCahaya == HIGH) ? "GELAP" : "TERANG");
  Serial.print(" | Temp: "); Serial.print(suhu, 1);
  Serial.print("C | Hum: "); Serial.print(kelembaban, 0); Serial.println("%");

  Serial.print("[STATUS] Mode: "); Serial.print(isAutoMode ? "AUTO" : "MANUAL");
  Serial.print(" | Dinamo: "); Serial.print(dinamoStatus); 
  Serial.print(" | Servo: "); Serial.print(currentServoPosition);
  Serial.print("° | Atap: "); Serial.print(currentRoofStatus);
  Serial.print(" | Buzzer: "); Serial.println(isBuzzerActive ? "ON" : "OFF");
  
  Serial.print("[KONEKSI] WiFi: "); Serial.print(wifiConnected ? "OK" : "DISCONNECTED");
  Serial.print(" | MQTT: "); Serial.println(mqttConnected ? "OK" : "DISCONNECTED");
  Serial.print("[FUZZY] "); Serial.println(fuzzyStatusCondition);
}
