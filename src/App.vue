<template>
  <div class="app-container">
    <!-- Header -->
    <header class="header">
      <h1>Jemuran Otomatis</h1>
      <p>Sistem Jemuran Pintar dengan Sensor Hujan & Cahaya</p>
    </header>

    <!-- Connection Bar -->
    <div class="connection-bar">
      <input 
        v-model="mqttBroker" 
        type="text" 
        placeholder="MQTT Broker (wss://broker.hivemq.com:8884/mqtt)"
        :disabled="isConnected"
      />
      <input 
        v-model="mqttTopic" 
        type="text" 
        placeholder="Topic (jemuran_otomatis/sensor/data)"
        :disabled="isConnected"
        class="topic-input"
      />
      <button 
        v-if="!isConnected" 
        @click="connect" 
        class="btn btn-primary"
        :disabled="isConnecting"
      >
        {{ isConnecting ? 'Connecting...' : 'Connect' }}
      </button>
      <button 
        v-else 
        @click="disconnect" 
        class="btn btn-danger"
      >
        Disconnect
      </button>
      <div class="connection-status">
        <span class="status-dot" :class="connectionStatusClass"></span>
        {{ connectionStatusText }}
      </div>
      <!-- Device Status -->
      <div v-if="isConnected" class="device-status" :class="deviceStatusClass">
        <span class="device-dot" :class="isDeviceOnline ? 'online' : 'offline'"></span>
        {{ deviceStatusText }}
      </div>
    </div>

    <!-- Waiting for Connection State -->
    <div v-if="!isConnected" class="waiting-state">
      <div class="waiting-card">
        <div class="waiting-icon">
          <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
            <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M13 10V3L4 14h7v7l9-11h-7z" />
          </svg>
        </div>
        <h2>Menunggu Koneksi MQTT</h2>
        <p>Hubungkan ESP32 Jemuran Otomatis Anda untuk memulai monitoring.</p>
        <div class="connection-steps">
          <div class="step">
            <span class="step-number">1</span>
            <span>Upload kode ke ESP32</span>
          </div>
          <div class="step">
            <span class="step-number">2</span>
            <span>Pastikan ESP32 terhubung ke broker</span>
          </div>
          <div class="step">
            <span class="step-number">3</span>
            <span>Masukkan broker & topic yang sama</span>
          </div>
        </div>
        <div class="info-box">
          <strong>Default Topic:</strong> jemuran_otomatis/sensor/data
        </div>
        <p v-if="connectionError" class="error-message">
          {{ connectionError }}
        </p>
      </div>
    </div>

    <!-- Device Offline Warning -->
    <div v-else-if="isConnected && !isDeviceOnline" class="device-offline-state">
      <div class="offline-card">
        <div class="offline-icon">📡</div>
        <h2>Perangkat Offline</h2>
        <p>ESP32 tidak mengirim data. Pastikan perangkat menyala dan terhubung ke WiFi.</p>
        <div class="last-seen">
          <span>Terakhir online:</span>
          <strong>{{ lastOnlineTime || 'Belum pernah' }}</strong>
        </div>
        <div class="tips">
          <p>💡 Tips:</p>
          <ul>
            <li>Cek power supply ESP32</li>
            <li>Pastikan WiFi tersedia</li>
            <li>Restart perangkat jika perlu</li>
          </ul>
        </div>
      </div>
    </div>

    <!-- Dashboard Grid (Only show when connected AND device online) -->
    <div v-else class="dashboard-grid">
      <!-- Mode Control Card -->
      <div class="card mode-card">
        <div class="card-header">
          <div class="card-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M10.325 4.317c.426-1.756 2.924-1.756 3.35 0a1.724 1.724 0 002.573 1.066c1.543-.94 3.31.826 2.37 2.37a1.724 1.724 0 001.065 2.572c1.756.426 1.756 2.924 0 3.35a1.724 1.724 0 00-1.066 2.573c.94 1.543-.826 3.31-2.37 2.37a1.724 1.724 0 00-2.572 1.065c-.426 1.756-2.924 1.756-3.35 0a1.724 1.724 0 00-2.573-1.066c-1.543.94-3.31-.826-2.37-2.37a1.724 1.724 0 00-1.065-2.572c-1.756-.426-1.756-2.924 0-3.35a1.724 1.724 0 001.066-2.573c-.94-1.543.826-3.31 2.37-2.37.996.608 2.296.07 2.572-1.065z" />
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M15 12a3 3 0 11-6 0 3 3 0 016 0z" />
            </svg>
          </div>
          <span class="card-title">Mode Kontrol</span>
        </div>
        <div class="mode-toggle-container">
          <div class="mode-toggle">
            <button 
              @click="setMode('auto')" 
              :class="['mode-btn', { active: currentMode === 'auto' }]"
            >
              Otomatis
            </button>
            <button 
              @click="setMode('manual')" 
              :class="['mode-btn', { active: currentMode === 'manual' }]"
            >
              Manual
            </button>
          </div>
          <p class="mode-description">
            {{ currentMode === 'auto' 
              ? 'Atap dikontrol otomatis berdasarkan cuaca' 
              : 'Anda dapat mengontrol atap secara manual' }}
          </p>
        </div>
      </div>

      <!-- Servo/Roof Control Card -->
      <div class="card servo-card" :class="{ disabled: currentMode === 'auto' }">
        <div class="card-header">
          <div class="card-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 12l2-2m0 0l7-7 7 7M5 10v10a1 1 0 001 1h3m10-11l2 2m-2-2v10a1 1 0 01-1 1h-3m-6 0a1 1 0 001-1v-4a1 1 0 011-1h2a1 1 0 011 1v4a1 1 0 001 1m-6 0h6" />
            </svg>
          </div>
          <span class="card-title">Kontrol Atap</span>
          <span v-if="currentMode === 'auto'" class="mode-badge auto">AUTO</span>
          <span v-else class="mode-badge manual">MANUAL</span>
        </div>
        <div class="servo-controls">
          <div class="servo-position">
            <span class="position-label">Posisi Atap:</span>
            <span class="position-value">{{ servoPosition }}°</span>
          </div>
          <div class="roof-status-display" :class="servoPosition > 10 ? 'open' : 'closed'">
            <span class="roof-text">{{ servoPosition > 10 ? 'TERBUKA' : 'TERTUTUP' }}</span>
          </div>
          <div class="servo-buttons">
            <button 
              @click="setServo('close')" 
              class="servo-btn close"
              :disabled="currentMode === 'auto'"
            >
              Tutup Atap
            </button>
            <button 
              @click="setServo('open')" 
              class="servo-btn open"
              :disabled="currentMode === 'auto'"
            >
              Buka Atap
            </button>
          </div>
          <p v-if="currentMode === 'auto'" class="servo-note">
            Ganti ke mode Manual untuk mengontrol atap
          </p>
        </div>
      </div>

      <!-- Dinamo Control Card -->
      <div class="card dinamo-card" :class="{ disabled: currentMode === 'auto' }">
        <div class="card-header">
          <div class="card-icon dinamo-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 4v5h.582m15.356 2A8.001 8.001 0 004.582 9m0 0H9m11 11v-5h-.581m0 0a8.003 8.003 0 01-15.357-2m15.357 2H15" />
            </svg>
          </div>
          <span class="card-title">Kontrol Jemuran (Dinamo)</span>
          <span v-if="currentMode === 'auto'" class="mode-badge auto">AUTO</span>
          <span v-else class="mode-badge manual">MANUAL</span>
        </div>
        <div class="dinamo-controls">
          <div class="dinamo-status-display" :class="dinamoStatusClass">
            <span class="dinamo-icon-display">{{ dinamoIcon }}</span>
            <span class="dinamo-text">{{ sensorData.dinamoStatus || 'STOP' }}</span>
          </div>
          <div class="dinamo-buttons">
            <button 
              @click="setDinamo('mundur')" 
              class="dinamo-btn mundur"
              :disabled="currentMode === 'auto'"
            >
              ⬅️ Mundur (Masuk)
            </button>
            <button 
              @click="setDinamo('stop')" 
              class="dinamo-btn stop"
              :disabled="currentMode === 'auto'"
            >
              ⏹️ Stop
            </button>
            <button 
              @click="setDinamo('maju')" 
              class="dinamo-btn maju"
              :disabled="currentMode === 'auto'"
            >
              Maju (Keluar) ➡️
            </button>
          </div>
          <p v-if="currentMode === 'auto'" class="servo-note">
            Ganti ke mode Manual untuk mengontrol dinamo
          </p>
        </div>
      </div>

      <!-- Rain Sensor Card -->
      <div class="card sensor-card" :class="sensorData.isRaining ? 'rain-active' : ''">
        <div class="card-header">
          <div class="card-icon rain-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 15a4 4 0 004 4h9a5 5 0 10-.1-9.999 5.002 5.002 0 10-9.78 2.096A4.001 4.001 0 003 15z" />
            </svg>
          </div>
          <span class="card-title">Sensor Hujan</span>
        </div>
        <div class="sensor-display">
          <div class="sensor-icon-large">
            {{ sensorData.isRaining ? '🌧️' : '☀️' }}
          </div>
          <div class="sensor-value" :class="sensorData.isRaining ? 'danger' : 'safe'">
            {{ sensorData.isRaining ? 'HUJAN' : 'CERAH' }}
          </div>
          <div class="sensor-indicator" :class="sensorData.isRaining ? 'active' : ''">
            <span class="indicator-dot"></span>
            {{ sensorData.isRaining ? 'Hujan Terdeteksi!' : 'Tidak Ada Hujan' }}
          </div>
        </div>
      </div>

      <!-- Light Sensor Card -->
      <div class="card sensor-card" :class="sensorData.isDark ? 'dark-active' : ''">
        <div class="card-header">
          <div class="card-icon light-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 3v1m0 16v1m9-9h-1M4 12H3m15.364 6.364l-.707-.707M6.343 6.343l-.707-.707m12.728 0l-.707.707M6.343 17.657l-.707.707M16 12a4 4 0 11-8 0 4 4 0 018 0z" />
            </svg>
          </div>
          <span class="card-title">Sensor Cahaya</span>
        </div>
        <div class="sensor-display">
          <div class="sensor-icon-large">
            {{ sensorData.isDark ? '🌙' : '☀️' }}
          </div>
          <div class="sensor-value" :class="sensorData.isDark ? 'warning' : 'safe'">
            {{ sensorData.isDark ? 'GELAP' : 'TERANG' }}
          </div>
          <div class="sensor-indicator" :class="sensorData.isDark ? 'active dark' : ''">
            <span class="indicator-dot"></span>
            {{ sensorData.isDark ? 'Kondisi Gelap' : 'Kondisi Terang' }}
          </div>
        </div>
      </div>

      <!-- Temperature Card -->
      <div class="card sensor-card temp-card">
        <div class="card-header">
          <div class="card-icon temp-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
            </svg>
          </div>
          <span class="card-title">Suhu (DHT11)</span>
        </div>
        <div class="sensor-display">
          <div class="sensor-icon-large">🌡️</div>
          <div class="sensor-value-large" :class="temperatureClass">
            {{ sensorData.temperature.toFixed(1) }}°C
          </div>
          <div class="sensor-indicator">
            <span class="indicator-dot"></span>
            {{ temperatureLabel }}
          </div>
        </div>
      </div>

      <!-- Humidity Card -->
      <div class="card sensor-card humidity-card">
        <div class="card-header">
          <div class="card-icon humidity-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M19.428 15.428a2 2 0 00-1.022-.547l-2.387-.477a6 6 0 00-3.86.517l-.318.158a6 6 0 01-3.86.517L6.05 15.21a2 2 0 00-1.806.547M8 4h8l-1 1v5.172a2 2 0 00.586 1.414l5 5c1.26 1.26.367 3.414-1.415 3.414H4.828c-1.782 0-2.674-2.154-1.414-3.414l5-5A2 2 0 009 10.172V5L8 4z" />
            </svg>
          </div>
          <span class="card-title">Kelembaban</span>
        </div>
        <div class="sensor-display">
          <div class="sensor-icon-large">💧</div>
          <div class="sensor-value-large" :class="humidityClass">
            {{ sensorData.humidity.toFixed(0) }}%
          </div>
          <div class="sensor-indicator">
            <span class="indicator-dot"></span>
            {{ humidityLabel }}
          </div>
        </div>
      </div>

      <!-- Fuzzy Logic Status Card -->
      <div class="card fuzzy-card">
        <div class="card-header">
          <div class="card-icon fuzzy-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9.663 17h4.673M12 3v1m6.364 1.636l-.707.707M21 12h-1M4 12H3m3.343-5.657l-.707-.707m2.828 9.9a5 5 0 117.072 0l-.548.547A3.374 3.374 0 0014 18.469V19a2 2 0 11-4 0v-.531c0-.895-.356-1.754-.988-2.386l-.548-.547z" />
            </svg>
          </div>
          <span class="card-title">Keputusan Fuzzy Logic</span>
        </div>
        <div class="fuzzy-display">
          <div class="fuzzy-condition" :class="fuzzyConditionClass">
            {{ sensorData.fuzzyCondition || 'Menunggu data...' }}
          </div>
          <div class="fuzzy-action">
            <span class="action-label">Aksi Atap:</span>
            <span class="action-value" :class="sensorData.isRoofOpen ? 'open' : 'closed'">
              {{ sensorData.isRoofOpen ? 'KELUAR (Jemur)' : 'MASUK (Aman)' }}
            </span>
          </div>
        </div>
      </div>

      <!-- Safety Status Card -->
      <div class="card status-card safety-card">
        <div class="card-header">
          <div class="card-icon safety-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12l2 2 4-4m5.618-4.016A11.955 11.955 0 0112 2.944a11.955 11.955 0 01-8.618 3.04A12.02 12.02 0 003 9c0 5.591 3.824 10.29 9 11.622 5.176-1.332 9-6.03 9-11.622 0-1.042-.133-2.052-.382-3.016z" />
            </svg>
          </div>
          <span class="card-title">Status Keamanan</span>
        </div>
        <div :class="['status-badge', safetyStatusType]">
          <span>{{ safetyText }}</span>
        </div>
        <p class="status-description">{{ safetyDescription }}</p>
      </div>

      <!-- Weather Condition Card -->
      <div class="card weather-card">
        <div class="card-header">
          <div class="card-icon weather-icon-header">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M3 15a4 4 0 004 4h9a5 5 0 10-.1-9.999 5.002 5.002 0 10-9.78 2.096A4.001 4.001 0 003 15z" />
            </svg>
          </div>
          <span class="card-title">Kondisi Cuaca</span>
        </div>
        <div class="weather-display">
          <div class="weather-icons">
            <span class="weather-icon" :class="{ active: sensorData.isRaining }">🌧️</span>
            <span class="weather-icon" :class="{ active: !sensorData.isRaining && !sensorData.isDark }">☀️</span>
            <span class="weather-icon" :class="{ active: sensorData.isDark }">🌙</span>
          </div>
          <div class="weather-text">
            {{ sensorData.isRaining ? 'Sedang Hujan 🌧️' : (sensorData.isDark ? 'Malam Hari 🌙' : 'Cerah Terang ☀️') }}
          </div>
        </div>
      </div>

      <!-- System Info Card -->
      <div class="card">
        <div class="card-header">
          <div class="card-icon info-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 19v-6a2 2 0 00-2-2H5a2 2 0 00-2 2v6a2 2 0 002 2h2a2 2 0 002-2zm0 0V9a2 2 0 012-2h2a2 2 0 012 2v10m-6 0a2 2 0 002 2h2a2 2 0 002-2m0 0V5a2 2 0 012-2h2a2 2 0 012 2v14a2 2 0 01-2 2h-2a2 2 0 01-2-2z" />
            </svg>
          </div>
          <span class="card-title">Info Sistem</span>
        </div>
        <div class="system-info">
          <div class="info-row">
            <span class="info-label">Device ID:</span>
            <span class="info-value">{{ sensorData.device_id || 'N/A' }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Loop Counter:</span>
            <span class="info-value">{{ sensorData.counter || 0 }}</span>
          </div>
          <div class="info-row">
            <span class="info-label">Mode:</span>
            <span class="info-value mode-indicator" :class="currentMode">
              {{ currentMode === 'auto' ? 'Otomatis' : 'Manual' }}
            </span>
          </div>
          <div class="info-row">
            <span class="info-label">Last Update:</span>
            <span class="info-value" :class="{ 'realtime-pulse': isRealtimeActive }">
              {{ lastUpdateTime || 'Menunggu...' }}
            </span>
          </div>
          <div class="info-row" v-if="updateLatency">
            <span class="info-label">Latency:</span>
            <span class="info-value latency-indicator" :class="latencyClass">
              {{ updateLatency }}ms
            </span>
          </div>
        </div>
      </div>

      <!-- History Card -->
      <div class="card history-card">
        <div class="card-header">
          <div class="card-icon history-icon">
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M12 8v4l3 3m6-3a9 9 0 11-18 0 9 9 0 0118 0z" />
            </svg>
          </div>
          <span class="card-title">Riwayat Aktivitas</span>
          
          <!-- Tab Buttons -->
          <div class="history-tabs">
            <button 
              :class="['tab-btn', { active: historyTab === 'realtime' }]"
              @click="historyTab = 'realtime'"
            >
              Real-time
            </button>
            <button 
              :class="['tab-btn', { active: historyTab === 'database' }]"
              @click="loadDatabaseHistory"
            >
              Database
            </button>
          </div>
          
          <button v-if="historyTab === 'realtime' && history.length > 0" @click="clearHistory" class="btn-clear">
            Hapus
          </button>
          <button v-if="historyTab === 'database'" @click="loadDatabaseHistory" class="btn-clear btn-refresh">
            Refresh
          </button>
        </div>
        
        <!-- Real-time History -->
        <div v-if="historyTab === 'realtime'">
          <div class="history-list" v-if="history.length > 0">
            <div class="history-header">
              <span class="history-col">Waktu</span>
              <span class="history-col">Hujan</span>
              <span class="history-col">Cahaya</span>
              <span class="history-col">Suhu</span>
              <span class="history-col">Lembab</span>
              <span class="history-col">Atap</span>
              <span class="history-col">Status Fuzzy</span>
            </div>
            <div 
              v-for="(item, index) in history" 
              :key="index"
              :class="['history-item', item.type]"
            >
              <span class="history-time">{{ item.time }}</span>
              <span class="history-weather">{{ item.isRaining ? 'Hujan' : 'Cerah' }}</span>
              <span class="history-light">{{ item.isDark ? 'Gelap' : 'Terang' }}</span>
              <span class="history-temp">{{ item.temperature?.toFixed(1) || '-' }}°</span>
              <span class="history-humid">{{ item.humidity?.toFixed(0) || '-' }}%</span>
              <span class="history-roof">{{ item.isRoofOpen ? 'Terbuka' : 'Tertutup' }}</span>
              <span class="history-status">{{ item.fuzzyCondition || item.status }}</span>
            </div>
          </div>
          <div class="empty-state" v-else>
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M9 12h6m-6 4h6m2 5H7a2 2 0 01-2-2V5a2 2 0 012-2h5.586a1 1 0 01.707.293l5.414 5.414a1 1 0 01.293.707V19a2 2 0 01-2 2z" />
            </svg>
            <p>Menunggu data dari sensor...</p>
          </div>
        </div>
        
        <!-- Database History -->
        <div v-else>
          <div v-if="isLoadingDbHistory" class="loading-state">
            <div class="spinner"></div>
            <p>Memuat data dari database...</p>
          </div>
          <div class="history-list" v-else-if="databaseHistory.length > 0">
            <div class="history-header">
              <span class="history-col">Waktu</span>
              <span class="history-col">Hujan</span>
              <span class="history-col">Cahaya</span>
              <span class="history-col">Suhu</span>
              <span class="history-col">Lembab</span>
              <span class="history-col">Atap</span>
              <span class="history-col">Status Fuzzy</span>
            </div>
            <div 
              v-for="(item, index) in databaseHistory" 
              :key="item.id || index"
              :class="['history-item', item.is_raining ? 'danger' : (item.is_dark ? 'warning' : 'success')]"
            >
              <span class="history-time">{{ formatDbTime(item.created_at) }}</span>
              <span class="history-weather">{{ item.is_raining ? 'Hujan' : 'Cerah' }}</span>
              <span class="history-light">{{ item.is_dark ? 'Gelap' : 'Terang' }}</span>
              <span class="history-temp">{{ item.temperature?.toFixed(1) || '-' }}°</span>
              <span class="history-humid">{{ item.humidity?.toFixed(0) || '-' }}%</span>
              <span class="history-roof">{{ item.is_roof_open ? 'Terbuka' : 'Tertutup' }}</span>
              <span class="history-status">{{ item.fuzzy_condition || (item.is_safe ? 'Aman' : 'Perlu Proteksi') }}</span>
            </div>
          </div>
          <div class="empty-state" v-else>
            <svg xmlns="http://www.w3.org/2000/svg" fill="none" viewBox="0 0 24 24" stroke="currentColor">
              <path stroke-linecap="round" stroke-linejoin="round" stroke-width="2" d="M4 7v10c0 2.21 3.582 4 8 4s8-1.79 8-4V7M4 7c0 2.21 3.582 4 8 4s8-1.79 8-4M4 7c0-2.21 3.582-4 8-4s8 1.79 8 4" />
            </svg>
            <p>Belum ada data tersimpan di database</p>
          </div>
        </div>
      </div>
    </div>
  </div>
</template>

<script setup>
import { ref, computed, onUnmounted, onMounted } from 'vue'
import mqtt from 'mqtt'
import { saveSensorData, getRecentSensorData } from './supabase.js'

// MQTT connection
const mqttBroker = ref('wss://broker.hivemq.com:8884/mqtt')
const mqttTopic = ref('jemuran_otomatis/sensor/data')
const mqttCommandTopic = ref('jemuran_otomatis/sensor/command')
const expectedDeviceId = ref('JEMURAN_001')
const client = ref(null)
const isConnected = ref(false)
const isConnecting = ref(false)
const connectionError = ref('')

// Mode control
const currentMode = ref('auto')
const servoPosition = ref(0)

// Sensor data
const sensorData = ref({
  device_id: '',
  counter: 0,
  isRaining: false,
  isDark: false,
  rainStatus: '',
  lightStatus: '',
  servoPosition: 0,
  isRoofOpen: false,
  roofStatus: '',
  weatherCondition: '',
  isSafe: true,
  safetyStatus: '',
  temperature: 0,
  humidity: 0,
  fuzzyCondition: '',
  dinamoStatus: 'STOP',
  isBuzzerOn: false
})

// History log (max 50 items)
const history = ref([])
const MAX_HISTORY = 50

// Supabase save control (save every N updates to avoid too many writes)
let saveCounter = 0
const SAVE_INTERVAL = 5  // Save to Supabase every 5 updates
const isSavingToSupabase = ref(false)

// Database history
const historyTab = ref('realtime')
const databaseHistory = ref([])
const isLoadingDbHistory = ref(false)

// Real-time tracking
const lastUpdateTime = ref('')
const updateLatency = ref(0)
const isRealtimeActive = ref(false)
let lastUpdateTimestamp = 0
let realtimeTimeout = null

// === DEVICE ONLINE/OFFLINE DETECTION ===
const isDeviceOnline = ref(false)
const lastOnlineTime = ref('')
let deviceTimeoutId = null
const DEVICE_TIMEOUT = 10000  // 10 detik tanpa data = offline

// Check device status periodically
function startDeviceTimeoutChecker() {
  if (deviceTimeoutId) clearInterval(deviceTimeoutId)
  
  deviceTimeoutId = setInterval(() => {
    const now = Date.now()
    const timeSinceLastUpdate = now - lastUpdateTimestamp
    
    if (lastUpdateTimestamp > 0 && timeSinceLastUpdate > DEVICE_TIMEOUT) {
      if (isDeviceOnline.value) {
        isDeviceOnline.value = false
        console.log('Device went offline (no data for 10 seconds)')
      }
    }
  }, 2000)  // Check every 2 seconds
}

function stopDeviceTimeoutChecker() {
  if (deviceTimeoutId) {
    clearInterval(deviceTimeoutId)
    deviceTimeoutId = null
  }
}

// Device status computed
const deviceStatusClass = computed(() => {
  return isDeviceOnline.value ? 'device-online' : 'device-offline'
})

const deviceStatusText = computed(() => {
  return isDeviceOnline.value ? 'Device Online' : 'Device Offline'
})

// Computed for latency indicator class
const latencyClass = computed(() => {
  if (updateLatency.value < 100) return 'excellent'
  if (updateLatency.value < 300) return 'good'
  if (updateLatency.value < 500) return 'fair'
  return 'slow'
})

// Connection status display
const connectionStatusClass = computed(() => {
  if (isConnected.value) return 'connected'
  if (isConnecting.value) return 'connecting'
  return 'disconnected'
})

const connectionStatusText = computed(() => {
  if (isConnected.value) return 'MQTT Connected'
  if (isConnecting.value) return 'Connecting...'
  return 'Disconnected'
})

// Safety status computation
const safetyStatusType = computed(() => {
  if (sensorData.value.isRaining) return 'danger'
  if (sensorData.value.isDark) return 'warning'
  return 'success'
})

const safetyText = computed(() => {
  if (sensorData.value.isRaining) return 'TIDAK AMAN'
  if (sensorData.value.isDark) return 'MALAM HARI'
  return 'AMAN'
})

const safetyDescription = computed(() => {
  if (sensorData.value.isRaining) {
    return 'Hujan terdeteksi! Pakaian perlu dilindungi.'
  }
  if (sensorData.value.isDark) {
    return 'Kondisi gelap/malam. Atap sebaiknya ditutup.'
  }
  return 'Cuaca cerah & terang. Aman untuk menjemur pakaian!'
})

// Temperature computed properties
const temperatureClass = computed(() => {
  const temp = sensorData.value.temperature
  if (temp >= 32) return 'danger'
  if (temp >= 28) return 'warning'
  return 'safe'
})

const temperatureLabel = computed(() => {
  const temp = sensorData.value.temperature
  if (temp >= 32) return 'Panas'
  if (temp >= 28) return 'Hangat'
  if (temp >= 22) return 'Normal'
  return 'Dingin'
})

// Humidity computed properties
const humidityClass = computed(() => {
  const hum = sensorData.value.humidity
  if (hum >= 70) return 'danger'
  if (hum >= 60) return 'warning'
  return 'safe'
})

const humidityLabel = computed(() => {
  const hum = sensorData.value.humidity
  if (hum >= 70) return 'Basah'
  if (hum >= 50) return 'Lembab'
  return 'Kering'
})

// Fuzzy condition class
const fuzzyConditionClass = computed(() => {
  const cond = sensorData.value.fuzzyCondition?.toLowerCase() || ''
  if (cond.includes('optimal') || cond.includes('sejuk')) return 'optimal'
  if (cond.includes('hujan') || cond.includes('bahaya')) return 'danger'
  if (cond.includes('malam') || cond.includes('gelap')) return 'warning'
  if (cond.includes('terik') || cond.includes('panas')) return 'hot'
  return 'normal'
})

// Dinamo status computed properties
const dinamoStatusClass = computed(() => {
  const status = sensorData.value.dinamoStatus
  if (status === 'MAJU') return 'maju'
  if (status === 'MUNDUR') return 'mundur'
  return 'stop'
})

const dinamoIcon = computed(() => {
  const status = sensorData.value.dinamoStatus
  if (status === 'MAJU') return '➡️'
  if (status === 'MUNDUR') return '⬅️'
  return '⏹️'
})

// Add to history
function addToHistory(data) {
  const now = new Date()
  const time = now.toLocaleTimeString('id-ID', { 
    hour: '2-digit', 
    minute: '2-digit',
    second: '2-digit'
  })
  
  // Determine type based on safety
  let type = 'success'
  if (data.isRaining) type = 'danger'
  else if (data.isDark) type = 'warning'
  
  history.value.unshift({
    time,
    isRaining: data.isRaining,
    isDark: data.isDark,
    isRoofOpen: data.isRoofOpen,
    mode: data.mode || currentMode.value,
    status: data.isSafe ? 'Aman' : 'Perlu Proteksi',
    temperature: data.temperature,
    humidity: data.humidity,
    fuzzyCondition: data.fuzzyCondition,
    type
  })
  
  // Keep only last MAX_HISTORY items
  if (history.value.length > MAX_HISTORY) {
    history.value.pop()
  }
}

// Save to Supabase
async function saveToSupabase(data) {
  if (isSavingToSupabase.value) return
  
  isSavingToSupabase.value = true
  try {
    const success = await saveSensorData({
      device_id: data.device_id,
      isRaining: data.isRaining,
      isDark: data.isDark,
      rainStatus: data.rainStatus,
      lightStatus: data.lightStatus,
      servoPosition: data.servoPosition,
      isRoofOpen: data.isRoofOpen,
      roofStatus: data.roofStatus,
      weatherCondition: data.weatherCondition,
      isSafe: data.isSafe,
      mode: data.mode,
      counter: data.counter,
      temperature: data.temperature,
      humidity: data.humidity,
      fuzzyCondition: data.fuzzy_cond || data.fuzzyCondition
    })
    if (success) {
      console.log('Data saved to Supabase')
    }
  } catch (error) {
    console.error('Failed to save to Supabase:', error)
  } finally {
    isSavingToSupabase.value = false
  }
}

// Clear history
function clearHistory() {
  history.value = []
}

// Load history from Supabase database
async function loadDatabaseHistory() {
  historyTab.value = 'database'
  isLoadingDbHistory.value = true
  
  try {
    const data = await getRecentSensorData(100)
    databaseHistory.value = data || []
    console.log('Loaded', data?.length || 0, 'records from database')
  } catch (error) {
    console.error('Failed to load database history:', error)
    databaseHistory.value = []
  } finally {
    isLoadingDbHistory.value = false
  }
}

// Format database timestamp to readable time
function formatDbTime(timestamp) {
  if (!timestamp) return '-'
  const date = new Date(timestamp)
  return date.toLocaleString('id-ID', {
    day: '2-digit',
    month: '2-digit',
    hour: '2-digit',
    minute: '2-digit'
  })
}

// Send command to ESP32 via MQTT
function sendCommand(command) {
  if (client.value && client.value.connected) {
    const topic = mqttCommandTopic.value
    client.value.publish(topic, JSON.stringify(command))
    console.log('Sent command to', topic, ':', command)
  }
}

// Set mode (auto/manual)
function setMode(mode) {
  currentMode.value = mode
  sendCommand({
    command: 'setMode',
    mode: mode
  })
}

// Set servo/roof position (manual mode only)
function setServo(action) {
  if (currentMode.value === 'manual') {
    sendCommand({
      command: 'setServo',
      action: action
    })
  }
}

// Set dinamo (motor) action (manual mode only)
function setDinamo(action) {
  if (currentMode.value === 'manual') {
    sendCommand({
      command: 'setDinamo',
      action: action
    })
  }
}

// MQTT connection
function connect() {
  if (isConnecting.value) return
  
  isConnecting.value = true
  connectionError.value = ''
  
  try {
    const clientId = 'web_jemuran_' + Math.random().toString(16).substring(2, 10)
    
    console.log('Connecting to MQTT broker:', mqttBroker.value)
    
    // Update command topic based on data topic
    const baseTopic = mqttTopic.value.replace('/data', '')
    mqttCommandTopic.value = baseTopic + '/command'
    
    client.value = mqtt.connect(mqttBroker.value, {
      clientId: clientId,
      clean: true,
      connectTimeout: 10000,
      reconnectPeriod: 5000
    })
    
    client.value.on('connect', () => {
      console.log('MQTT Connected!')
      isConnected.value = true
      isConnecting.value = false
      connectionError.value = ''
      
      // Subscribe to data topic
      client.value.subscribe(mqttTopic.value, (err) => {
        if (err) {
          console.error('Subscribe error:', err)
        } else {
          console.log('Subscribed to:', mqttTopic.value)
        }
      })
      
      // Reset data
      sensorData.value = {
        device_id: '',
        counter: 0,
        isRaining: false,
        isDark: false,
        rainStatus: '',
        lightStatus: '',
        servoPosition: 0,
        isRoofOpen: false,
        roofStatus: '',
        weatherCondition: '',
        isSafe: true,
        safetyStatus: '',
        temperature: 0,
        humidity: 0,
        fuzzyCondition: '',
        dinamoStatus: 'STOP',
        isBuzzerOn: false
      }
      history.value = []
      currentMode.value = 'auto'
      servoPosition.value = 0
      
      // Start device timeout checker
      isDeviceOnline.value = false
      lastUpdateTimestamp = 0
      startDeviceTimeoutChecker()
    })
    
    client.value.on('message', (topic, message) => {
      try {
        const data = JSON.parse(message.toString())
        console.log('Received:', data)
        
        // Filter by device_id if present
        if (data.device_id && data.device_id !== expectedDeviceId.value) {
          console.log('Ignored message from different device:', data.device_id)
          return
        }
        
        // Update sensor data
        sensorData.value = {
          device_id: data.device_id || '',
          counter: data.counter || 0,
          isRaining: data.isRaining || false,
          isDark: data.isDark || false,
          rainStatus: data.rainStatus || '',
          lightStatus: data.lightStatus || '',
          servoPosition: data.servoPosition || 0,
          isRoofOpen: data.isRoofOpen || false,
          roofStatus: data.roofStatus || '',
          weatherCondition: data.weatherCondition || '',
          isSafe: data.isSafe || false,
          safetyStatus: data.safetyStatus || '',
          temperature: data.temperature || 0,
          humidity: data.humidity || 0,
          fuzzyCondition: data.fuzzy_cond || data.fuzzyCondition || '',
          dinamoStatus: data.dinamoStatus || 'STOP',
          isBuzzerOn: data.isBuzzerOn || false
        }
        
        // Update mode and servo position
        if (data.mode) {
          currentMode.value = data.mode
        }
        if (data.servoPosition !== undefined) {
          servoPosition.value = data.servoPosition
        }
        
        // Real-time tracking - update timestamp
        const now = new Date()
        lastUpdateTime.value = now.toLocaleTimeString('id-ID', { 
          hour: '2-digit', 
          minute: '2-digit',
          second: '2-digit',
          fractionalSecondDigits: 2
        })
        
        // Calculate latency (time between updates)
        const currentTimestamp = now.getTime()
        if (lastUpdateTimestamp > 0) {
          updateLatency.value = currentTimestamp - lastUpdateTimestamp
        }
        lastUpdateTimestamp = currentTimestamp
        
        // === SET DEVICE ONLINE ===
        if (!isDeviceOnline.value) {
          isDeviceOnline.value = true
          console.log('Device is now online!')
        }
        lastOnlineTime.value = now.toLocaleTimeString('id-ID', { 
          hour: '2-digit', 
          minute: '2-digit',
          second: '2-digit'
        })
        
        // Activate real-time pulse effect
        isRealtimeActive.value = true
        if (realtimeTimeout) clearTimeout(realtimeTimeout)
        realtimeTimeout = setTimeout(() => {
          isRealtimeActive.value = false
        }, 200)
        
        // Add to history
        addToHistory(sensorData.value)
        
        // Save to Supabase (every SAVE_INTERVAL updates to reduce writes)
        saveCounter++
        if (saveCounter >= SAVE_INTERVAL) {
          saveCounter = 0
          saveToSupabase(data)
        }
      } catch (e) {
        console.error('Error parsing message:', e)
      }
    })
    
    client.value.on('error', (error) => {
      console.error('MQTT error:', error)
      connectionError.value = 'Connection error: ' + error.message
      isConnecting.value = false
    })
    
    client.value.on('close', () => {
      console.log('MQTT connection closed')
      isConnected.value = false
      isConnecting.value = false
    })
    
    // Connection timeout
    setTimeout(() => {
      if (!isConnected.value && isConnecting.value) {
        connectionError.value = 'Connection timeout. Check broker address.'
        client.value?.end()
        isConnecting.value = false
      }
    }, 10000)
    
  } catch (error) {
    console.error('Connection error:', error)
    connectionError.value = 'Error: ' + error.message
    isConnecting.value = false
  }
}

function disconnect() {
  stopDeviceTimeoutChecker()
  isDeviceOnline.value = false
  
  if (client.value) {
    client.value.end()
    client.value = null
  }
  isConnected.value = false
  isConnecting.value = false
}

// Cleanup on unmount
onUnmounted(() => {
  stopDeviceTimeoutChecker()
  disconnect()
})
</script>
