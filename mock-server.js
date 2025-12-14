/**
 * Mock MQTT Publisher for Testing with Manual Mode Support
 * Simulates ESP32 ultrasonic sensor data via MQTT
 * 
 * Run: node mock-server.js
 */

import mqtt from 'mqtt';

const BROKER = 'wss://broker.hivemq.com:8884/mqtt';
const TOPIC_DATA = 'afiqa08/rumah/sensor/data';
const TOPIC_COMMAND = 'afiqa08/rumah/sensor/command';

console.log(`🚀 Mock ESP32 MQTT Publisher with Manual Mode`);
console.log(`📡 Broker: ${BROKER}`);
console.log(`📬 Data Topic: ${TOPIC_DATA}`);
console.log(`📥 Command Topic: ${TOPIC_COMMAND}\n`);

// Simulate sensor data
let counter = 0;
let distance = 150;
let direction = -1;
const BATAS_BAHAYA = 20;
const POSISI_TERTUTUP = 0;
const POSISI_TERBUKA = 90;

// Mode and Servo state
let isAutoMode = true;
let servoPosition = 0;

// Connect to MQTT broker
const clientId = 'mock_esp32_' + Math.random().toString(16).substring(2, 10);
const client = mqtt.connect(BROKER, {
  clientId: clientId,
  clean: true
});

// Broadcast sensor data
function sendData() {
  // Determine status
  let status = '';
  if (distance < BATAS_BAHAYA && distance > 0) {
    status = 'BAHAYA (< 20cm)';
    counter = 0;
  } else if (distance >= BATAS_BAHAYA) {
    if (counter < 30) {
      status = 'Aman (Wait timeout)';
    } else {
      status = 'STANDBY (Aman)';
    }
  }
  
  // Auto servo control
  if (isAutoMode) {
    if (distance < BATAS_BAHAYA && distance > 0) {
      servoPosition = POSISI_TERBUKA;
    } else if (distance >= BATAS_BAHAYA) {
      servoPosition = POSISI_TERTUTUP;
    }
  }
  
  counter++;
  
  const data = {
    distance: Math.round(distance),
    counter: counter,
    status: status,
    mode: isAutoMode ? 'auto' : 'manual',
    servoPosition: servoPosition
  };
  
  client.publish(TOPIC_DATA, JSON.stringify(data));
  
  const modeIcon = isAutoMode ? '🤖' : '🎮';
  console.log(`${modeIcon} Jarak=${Math.round(distance)}cm | Servo=${servoPosition}° | ${status}`);
}

client.on('connect', () => {
  console.log('✅ Connected to MQTT broker!\n');
  
  // Subscribe to command topic
  client.subscribe(TOPIC_COMMAND, (err) => {
    if (!err) {
      console.log(`📥 Subscribed to: ${TOPIC_COMMAND}`);
      console.log('Sending simulated sensor data every second...\n');
    }
  });
  
  // Send data every second
  setInterval(() => {
    // Simulate distance changes
    distance += direction * (Math.random() * 15 + 3);
    
    if (distance <= 10) {
      direction = 1;
      distance = 10;
    } else if (distance >= 180) {
      direction = -1;
      distance = 180;
    }
    
    sendData();
  }, 1000);
});

// Handle incoming commands
client.on('message', (topic, message) => {
  if (topic === TOPIC_COMMAND) {
    try {
      const cmd = JSON.parse(message.toString());
      console.log('📥 Command received:', cmd);
      
      if (cmd.command === 'setMode') {
        if (cmd.mode === 'auto') {
          isAutoMode = true;
          console.log('>>> Mode: OTOMATIS');
        } else if (cmd.mode === 'manual') {
          isAutoMode = false;
          console.log('>>> Mode: MANUAL');
        }
      }
      
      if (cmd.command === 'setServo' && !isAutoMode) {
        if (cmd.action === 'open') {
          servoPosition = POSISI_TERBUKA;
          console.log('>>> [MANUAL] Servo TERBUKA (90°)');
        } else if (cmd.action === 'close') {
          servoPosition = POSISI_TERTUTUP;
          console.log('>>> [MANUAL] Servo TERTUTUP (0°)');
        }
        // Send immediate update
        sendData();
      }
    } catch (e) {
      console.error('Error parsing command:', e);
    }
  }
});

client.on('error', (error) => {
  console.error('❌ MQTT Error:', error);
});

console.log('Connecting to broker...\n');
