#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>
#include <FirebaseESP8266.h>
#include <NTPClient.h>
#include <WiFiUdp.h>
#include <ArduinoJson.h>

// Sensor definitions
#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

// WiFi credentials
const char* ssid = "iPhone (Beksultan)";
const char* password = "12341234";

// Firebase configuration
#define FIREBASE_HOST "aosproject-95c08-default-rtdb.firebaseio.com"
#define FIREBASE_AUTH "AIzaSyCweSP6qYhgCR2LNmqNwkwkpG6X2_AYpW0"

FirebaseData firebaseData;
FirebaseConfig fbConfig;
FirebaseAuth fbAuth;
FirebaseJson json;
FirebaseJsonArray jsonArray;
ESP8266WebServer server(80);

// NTP Client for timestamps
WiFiUDP ntpUDP;
NTPClient timeClient(ntpUDP, "pool.ntp.org", 0, 60000);

// Data collection variables
unsigned long lastDataSend = 0;
const unsigned long DATA_INTERVAL = 60000; // Send data every minute
int dataPointsCount = 0;

// Firebase data structure paths
const String FB_AIR_QUALITY_DATA = "/air_quality_data";
const String FB_LATEST_READING = "/latest_reading";
const String FB_DAILY_SUMMARY = "/daily_summary";
const String FB_AI_ADVICE = "/ai_advice";

// Air quality history for AI analysis
struct AirQualityData {
  float temperature;
  float humidity;
  int mq135Value;
  String quality;
  unsigned long timestamp;
};

AirQualityData recentReadings[24]; // Store last 24 readings for analysis
int readingIndex = 0;

void setup() {
  Serial.begin(115200);
  dht.begin();
  
  // Connect to WiFi
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }
  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.println(WiFi.localIP());
  
  // Initialize Firebase
  fbConfig.host = FIREBASE_HOST;
  fbConfig.api_key = FIREBASE_AUTH;
  fbAuth.user.email = "esp8266@example.com";
  fbAuth.user.password = "esp8266password";
  Firebase.begin(&fbConfig, &fbAuth);
  Firebase.reconnectWiFi(true);
  
  // Initialize NTP client
  timeClient.begin();
  timeClient.update();
  
  // Web server routes
  server.on("/", handleRoot);
  server.on("/data", handleData);
  server.on("/advice", handleAdvice);
  server.on("/history", handleHistory);
  server.on("/firebaseData", handleFirebaseData);
  
  server.begin();
  Serial.println("HTTP server started");
  
  // Initialize readings array
  for(int i = 0; i < 24; i++) {
    recentReadings[i].timestamp = 0;
  }
}

void loop() {
  server.handleClient();
  timeClient.update();
  
  // Send data to Firebase periodically
  if (millis() - lastDataSend > DATA_INTERVAL) {
    collectAndSendData();
    lastDataSend = millis();
    
    // Every 24 data points (approximately every 24 minutes), update the AI advice
    if (dataPointsCount % 24 == 0 && dataPointsCount > 0) {
      updateAIAdvice();
    }
  }
}

void collectAndSendData() {
  float temperature = dht.readTemperature();
  float humidity = dht.readHumidity();
  int mq135Value = analogRead(A0);
  String quality = evaluateAir(mq135Value);
  unsigned long timestamp = timeClient.getEpochTime();
  
  // Check if readings are valid
  if (isnan(temperature) || isnan(humidity)) {
    Serial.println("Failed to read from DHT sensor!");
    return;
  }
  
  // Store in local history
  recentReadings[readingIndex] = {temperature, humidity, mq135Value, quality, timestamp};
  readingIndex = (readingIndex + 1) % 24;
  
  // Create JSON object for Firebase
  json.clear();
  json.set("temperature", temperature);
  json.set("humidity", humidity);
  json.set("mq135_raw", mq135Value);
  json.set("air_quality", quality);
  json.set("timestamp", timestamp);
  json.set("device_id", "ESP8266_001");
  
  // Send to Firebase
  String path = FB_AIR_QUALITY_DATA + "/" + String(timestamp);
  if (Firebase.setJSON(firebaseData, path, json)) {
    Serial.println("Data sent to Firebase successfully");
    dataPointsCount++;
  } else {
    Serial.println("Failed to send data to Firebase");
    Serial.println(firebaseData.errorReason());
  }
  
  // Also update latest reading
  if (Firebase.setJSON(firebaseData, FB_LATEST_READING, json)) {
    Serial.println("Latest reading updated");
  }
  
  // Update daily summary
  updateDailySummary(temperature, humidity, mq135Value, quality, timestamp);
}

void updateDailySummary(float temperature, float humidity, int mq135Value, String quality, unsigned long timestamp) {
  // Get the current day (YYYY-MM-DD format)
  time_t rawtime = timestamp;
  struct tm * ti;
  ti = localtime(&rawtime);
  
  char dateStr[11];
  sprintf(dateStr, "%04d-%02d-%02d", ti->tm_year + 1900, ti->tm_mon + 1, ti->tm_mday);
  String today = String(dateStr);
  
  // Path to today's summary
  String path = FB_DAILY_SUMMARY + "/" + today;
  
  // Try to get current summary data
  if (Firebase.getJSON(firebaseData, path)) {
    FirebaseJson currentSummary = firebaseData.jsonObject();
    FirebaseJsonData countData;
    FirebaseJsonData tempSumData;
    FirebaseJsonData humSumData;
    FirebaseJsonData mq135SumData;
    FirebaseJsonData maxTempData;
    FirebaseJsonData minTempData;
    FirebaseJsonData maxHumData;
    FirebaseJsonData minHumData;
    FirebaseJsonData maxMQ135Data;
    FirebaseJsonData minMQ135Data;
    
    currentSummary.get(countData, "count");
    currentSummary.get(tempSumData, "temperature_sum");
    currentSummary.get(humSumData, "humidity_sum");
    currentSummary.get(mq135SumData, "mq135_sum");
    currentSummary.get(maxTempData, "max_temperature");
    currentSummary.get(minTempData, "min_temperature");
    currentSummary.get(maxHumData, "max_humidity");
    currentSummary.get(minHumData, "min_humidity");
    currentSummary.get(maxMQ135Data, "max_mq135");
    currentSummary.get(minMQ135Data, "min_mq135");
    
    int count = countData.success ? countData.intValue + 1 : 1;
    float tempSum = tempSumData.success ? tempSumData.floatValue + temperature : temperature;
    float humSum = humSumData.success ? humSumData.floatValue + humidity : humidity;
    int mq135Sum = mq135SumData.success ? mq135SumData.intValue + mq135Value : mq135Value;
    
    float maxTemp = maxTempData.success ? max(maxTempData.floatValue, temperature) : temperature;
    float minTemp = minTempData.success ? min(minTempData.floatValue, temperature) : temperature;
    float maxHum = maxHumData.success ? max(maxHumData.floatValue, humidity) : humidity;
    float minHum = minHumData.success ? min(minHumData.floatValue, humidity) : humidity;
    int maxMQ135 = maxMQ135Data.success ? max(maxMQ135Data.intValue, mq135Value) : mq135Value;
    int minMQ135 = minMQ135Data.success ? min(minMQ135Data.intValue, mq135Value) : mq135Value;
    
    // Calculate qualities count
    int goodCount = 0, normalCount = 0, moderateCount = 0, badCount = 0;
    FirebaseJsonData qualityCountData;
    
    currentSummary.get(qualityCountData, "quality_good");
    goodCount = qualityCountData.success ? qualityCountData.intValue : 0;
    currentSummary.get(qualityCountData, "quality_normal");
    normalCount = qualityCountData.success ? qualityCountData.intValue : 0;
    currentSummary.get(qualityCountData, "quality_moderate");
    moderateCount = qualityCountData.success ? qualityCountData.intValue : 0;
    currentSummary.get(qualityCountData, "quality_bad");
    badCount = qualityCountData.success ? qualityCountData.intValue : 0;
    
    if (quality == "Good") goodCount++;
    else if (quality == "Normal") normalCount++;
    else if (quality == "Moderate") moderateCount++;
    else if (quality == "Bad") badCount++;
    
    // Update summary
    json.clear();
    json.set("count", count);
    json.set("temperature_sum", tempSum);
    json.set("humidity_sum", humSum);
    json.set("mq135_sum", mq135Sum);
    json.set("avg_temperature", tempSum / count);
    json.set("avg_humidity", humSum / count);
    json.set("avg_mq135", mq135Sum / count);
    json.set("max_temperature", maxTemp);
    json.set("min_temperature", minTemp);
    json.set("max_humidity", maxHum);
    json.set("min_humidity", minHum);
    json.set("max_mq135", maxMQ135);
    json.set("min_mq135", minMQ135);
    json.set("quality_good", goodCount);
    json.set("quality_normal", normalCount);
    json.set("quality_moderate", moderateCount);
    json.set("quality_bad", badCount);
    json.set("last_updated", timestamp);
    
  } else {
    // Create new summary
    json.clear();
    json.set("count", 1);
    json.set("temperature_sum", temperature);
    json.set("humidity_sum", humidity);
    json.set("mq135_sum", mq135Value);
    json.set("avg_temperature", temperature);
    json.set("avg_humidity", humidity);
    json.set("avg_mq135", mq135Value);
    json.set("max_temperature", temperature);
    json.set("min_temperature", temperature);
    json.set("max_humidity", humidity);
    json.set("min_humidity", humidity);
    json.set("max_mq135", mq135Value);
    json.set("min_mq135", mq135Value);
    json.set("quality_good", quality == "Good" ? 1 : 0);
    json.set("quality_normal", quality == "Normal" ? 1 : 0);
    json.set("quality_moderate", quality == "Moderate" ? 1 : 0);
    json.set("quality_bad", quality == "Bad" ? 1 : 0);
    json.set("last_updated", timestamp);
  }
  
  // Save updated summary
  if (Firebase.setJSON(firebaseData, path, json)) {
    Serial.println("Daily summary updated");
  } else {
    Serial.println("Failed to update daily summary");
    Serial.println(firebaseData.errorReason());
  }
}

void updateAIAdvice() {
  // Get the last 3-7 days of summaries for AI analysis
  time_t now = timeClient.getEpochTime();
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  
  json.clear();
  
  // Generate advanced advice based on daily summaries
  String advanced_advice = generateAdvancedAdvice();
  
  json.set("advice", advanced_advice);
  json.set("timestamp", now);
  
  if (Firebase.setJSON(firebaseData, FB_AI_ADVICE, json)) {
    Serial.println("AI advice updated in Firebase");
  } else {
    Serial.println("Failed to update AI advice");
    Serial.println(firebaseData.errorReason());
  }
}

String generateAdvancedAdvice() {
  String advice = "";
  
  // Try to get current day's summary
  time_t now = timeClient.getEpochTime();
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  
  char dateStr[11];
  sprintf(dateStr, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  String today = String(dateStr);
  
  String path = FB_DAILY_SUMMARY + "/" + today;
  
  if (Firebase.getJSON(firebaseData, path)) {
    FirebaseJson summary = firebaseData.jsonObject();
    FirebaseJsonData avgTempData, avgHumData, avgMQ135Data;
    FirebaseJsonData qualityGoodData, qualityNormalData, qualityModerateData, qualityBadData;
    
    summary.get(avgTempData, "avg_temperature");
    summary.get(avgHumData, "avg_humidity");
    summary.get(avgMQ135Data, "avg_mq135");
    summary.get(qualityGoodData, "quality_good");
    summary.get(qualityNormalData, "quality_normal");
    summary.get(qualityModerateData, "quality_moderate");
    summary.get(qualityBadData, "quality_bad");
    
    float avgTemp = avgTempData.success ? avgTempData.floatValue : 0;
    float avgHum = avgHumData.success ? avgHumData.floatValue : 0;
    int avgMQ135 = avgMQ135Data.success ? avgMQ135Data.intValue : 0;
    int goodCount = qualityGoodData.success ? qualityGoodData.intValue : 0;
    int normalCount = qualityNormalData.success ? qualityNormalData.intValue : 0;
    int moderateCount = qualityModerateData.success ? qualityModerateData.intValue : 0;
    int badCount = qualityBadData.success ? qualityBadData.intValue : 0;
    
    int totalReadings = goodCount + normalCount + moderateCount + badCount;
    
    advice += "🧠 Анализ ИИ на основе " + String(totalReadings) + " показаний за сегодня:\n\n";
    
    // Temperature analysis
    advice += "🌡️ Температура: Средняя " + String(avgTemp, 1) + "°C\n";
    if (avgTemp < 18) {
      advice += "• Помещение стабильно холодное. Рассмотрите возможность регулировки отопления.\n";
    } else if (avgTemp > 26) {
      advice += "• Помещение стабильно теплое. Улучшите охлаждение или вентиляцию.\n";
    } else {
      advice += "• Уровень температуры оптимален для комфорта и здоровья.\n";
    }
    
    // Humidity analysis
    advice += "\n💧 Влажность: Средняя " + String(avgHum, 1) + "%\n";
    if (avgHum < 30) {
      advice += "• Воздух стабильно сухой. Используйте увлажнитель для предотвращения проблем с дыханием и статическим электричеством.\n";
    } else if (avgHum > 60) {
      advice += "• Уровень влажности высокий. Следите за конденсатом и ростом плесени. Рассмотрите возможность использования осушителя.\n";
    } else {
      advice += "• Уровень влажности находится в идеальном диапазоне для комфорта и здоровья.\n";
    }
    
    // Air quality analysis
    advice += "\n🌬️ Качество воздуха:\n";
    if (badCount > 0) {
      int badPercentage = (badCount * 100) / totalReadings;
      advice += "• ТРЕВОГА: Плохое качество воздуха обнаружено в " + String(badPercentage) + "% сегодняшних показаний!\n";
      advice += "• Необходимы срочные меры: Проверьте источники загрязнения (готовка, химические вещества и т.д.)\n";
      advice += "• Рекомендуется: Использовать воздухоочиститель, увеличить вентиляцию и ограничить доступ наружного воздуха, если внешнее загрязнение высокое.\n";
    } else if (moderateCount > totalReadings/2) {
      advice += "• Качество воздуха было средним большую часть сегодняшнего дня.\n";
      advice += "• Рекомендуется: Увеличить циклы вентиляции и проверить возможные источники загрязнения.\n";
    } else if (goodCount > totalReadings/2) {
      advice += "• Качество воздуха было хорошим большую часть сегодняшнего дня. Продолжайте текущие практики.\n";
    }
    
    // Correlations and patterns
    advice += "\n🔄 Обнаруженные закономерности:\n";
    if (avgHum > 60 && avgMQ135 > 400) {
      advice += "• Высокая влажность коррелирует с худшими показаниями качества воздуха.\n";
      advice += "• Это может указывать на рост плесени или бактерий. Проверьте влажные участки.\n";
    }
    if (avgTemp > 26 && avgMQ135 > 400) {
      advice += "• Более высокие температуры коррелируют с худшим качеством воздуха.\n";
      advice += "• Это может указывать на увеличение выбросов ЛОС из материалов при высоких температурах.\n";
    }
    
    // Compare with recent readings
    float currentTemp = dht.readTemperature();
    float currentHumidity = dht.readHumidity();
    int currentMQ135 = analogRead(A0);
    
    advice += "\n📊 Текущие vs Средние показатели:\n";
    if (abs(currentTemp - avgTemp) > 3) {
      advice += "• Текущая температура (" + String(currentTemp, 1) + "°C) показывает значительное изменение от среднего за сегодня.\n";
    }
    if (abs(currentHumidity - avgHum) > 10) {
      advice += "• Текущая влажность (" + String(currentHumidity, 1) + "%) показывает значительное изменение от среднего за сегодня.\n";
    }
    if (abs(currentMQ135 - avgMQ135) > 100) {
      String change = currentMQ135 > avgMQ135 ? "ухудшается" : "улучшается";
      advice += "• Текущее качество воздуха " + change + " по сравнению со средним за сегодня.\n";
    }
    
  } else {
    // If no data is available yet, provide basic advice
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int mq135 = analogRead(A0);
    String quality = evaluateAir(mq135);
    
    advice = "Недостаточно исторических данных. Базовые рекомендации на основе текущих показаний:\n\n";
    advice += generateAdvice(t, h, mq135, quality);
  }
  
  return advice;
}

void handleRoot() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int mq135 = analogRead(A0);
  String quality = evaluateAir(mq135);
  String advice = generateAdvice(t, h, mq135, quality);
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta http-equiv='refresh' content='30'>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>";
  html += "body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; box-shadow: 0 2px 10px rgba(0,0,0,0.1); }";
  html += ".metric { background: #e8f4fd; padding: 15px; margin: 10px 0; border-radius: 5px; border-left: 4px solid #2196F3; }";
  html += ".advice { background: #f0f8e8; padding: 15px; margin: 20px 0; border-radius: 5px; border-left: 4px solid #4CAF50; }";
  html += ".good { border-left-color: #4CAF50; }";
  html += ".normal { border-left-color: #FF9800; }";
  html += ".moderate { border-left-color: #FF5722; }";
  html += ".bad { border-left-color: #F44336; }";
  html += "nav { margin-bottom: 20px; }";
  html += "nav a { margin-right: 15px; padding: 8px 16px; background: #2196F3; color: white; text-decoration: none; border-radius: 4px; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🌡️ Умный монитор качества воздуха</h1>";
  
  html += "<nav>";
  html += "<a href='/'>Панель</a>";
  html += "<a href='/data'>Данные</a>";
  html += "<a href='/advice'>ИИ-советы</a>";
  html += "<a href='/history'>История</a>";
  html += "<a href='/firebaseData'>Firebase</a>";
  html += "</nav>";
  
  html += "<div class='metric'>";
  html += "<h2>🌡️ Температура: " + String(t, 1) + " °C</h2>";
  html += "</div>";
  
  html += "<div class='metric'>";
  html += "<h2>💧 Влажность: " + String(h, 1) + " %</h2>";
  html += "</div>";
  
  html += "<div class='metric'>";
  html += "<h2>🌬️ Показания MQ135: " + String(mq135) + "</h2>";
  html += "</div>";
  
  String qualityClass = qualityToClassName(quality);
  html += "<div class='metric " + qualityClass + "'>";
  html += "<h2>🏭 Качество воздуха: " + translateQuality(quality) + "</h2>";
  html += "</div>";
  
  html += "<div class='advice'>";
  html += "<h2>🤖 Рекомендации ИИ:</h2>";
  html += "<p>" + advice + "</p>";
  html += "</div>";
  
  html += "<p><small>Собрано измерений: " + String(dataPointsCount) + " | Последнее обновление: " + timeClient.getFormattedTime() + "</small></p>";
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleData() {
  float t = dht.readTemperature();
  float h = dht.readHumidity();
  int mq135 = analogRead(A0);
  String quality = evaluateAir(mq135);
  
  DynamicJsonDocument doc(1024);
  doc["temperature"] = t;
  doc["humidity"] = h;
  doc["mq135_raw"] = mq135;
  doc["air_quality"] = quality;
  doc["timestamp"] = timeClient.getEpochTime();
  doc["formatted_time"] = timeClient.getFormattedTime();
  
  String jsonString;
  serializeJson(doc, jsonString);
  
  server.send(200, "application/json", jsonString);
}

void handleAdvice() {
  // Attempt to get AI advice from Firebase
  String aiAdvice = "";
  bool hasFirebaseAdvice = false;
  
  if (Firebase.getJSON(firebaseData, FB_AI_ADVICE)) {
    FirebaseJson adviceJson = firebaseData.jsonObject();
    FirebaseJsonData adviceData;
    adviceJson.get(adviceData, "advice");
    
    if (adviceData.success) {
      aiAdvice = adviceData.stringValue;
      hasFirebaseAdvice = true;
    }
  }
  
  // If no Firebase advice, generate locally
  if (!hasFirebaseAdvice) {
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int mq135 = analogRead(A0);
    String quality = evaluateAir(mq135);
    
    aiAdvice = generateAdvice(t, h, mq135, quality);
  }
  
  String trends = analyzeTrends();
  
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".advice-section { background: #f0f8e8; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #4CAF50; }";
  html += "pre { white-space: pre-wrap; font-family: inherit; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>🤖 ИИ-советник по качеству воздуха</h1>";
  html += "<a href='/'>← Назад на панель</a>";
  
  html += "<div class='advice-section'>";
  if (hasFirebaseAdvice) {
    html += "<h2>Расширенный анализ ИИ:</h2>";
    html += "<pre>" + aiAdvice + "</pre>";
  } else {
    html += "<h2>Текущие рекомендации:</h2>";
    html += "<p>" + aiAdvice + "</p>";
  }
  html += "</div>";
  
  html += "<div class='advice-section'>";
  html += "<h2>Анализ тенденций:</h2>";
  html += "<p>" + trends + "</p>";
  html += "</div>";
  
  html += "</div></body></html>";
  
  server.send(200, "text/html", html);
}

void handleHistory() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>body { font-family: Arial, sans-serif; margin: 20px; }";
  html += "table { width: 100%; border-collapse: collapse; }";
  html += "th, td { border: 1px solid #ddd; padding: 8px; text-align: left; }";
  html += "th { background-color: #f2f2f2; }";
  html += "</style></head><body>";
  
  html += "<h1>📊 История измерений</h1>";
  html += "<a href='/'>← Назад на панель</a>";
  html += "<table><tr><th>Время</th><th>Темп (°C)</th><th>Влажность (%)</th><th>MQ135</th><th>Качество</th></tr>";
  
  for(int i = 0; i < 24; i++) {
    int idx = (readingIndex - 1 - i + 24) % 24; // Display newest first
    if(recentReadings[idx].timestamp > 0) {
      // Convert epoch to readable time
      time_t rawtime = recentReadings[idx].timestamp;
      struct tm * ti;
      ti = localtime(&rawtime);
      char timeStr[20];
      sprintf(timeStr, "%02d:%02d:%02d", ti->tm_hour, ti->tm_min, ti->tm_sec);
      
      html += "<tr>";
      html += "<td>" + String(timeStr) + "</td>";
      html += "<td>" + String(recentReadings[idx].temperature, 1) + "</td>";
      html += "<td>" + String(recentReadings[idx].humidity, 1) + "</td>";
      html += "<td>" + String(recentReadings[idx].mq135Value) + "</td>";
      html += "<td>" + translateQuality(recentReadings[idx].quality) + "</td>";
      html += "</tr>";
    }
  }
  
  html += "</table></body></html>";
  server.send(200, "text/html", html);
}

void handleFirebaseData() {
  String html = "<!DOCTYPE html><html><head>";
  html += "<meta name='viewport' content='width=device-width, initial-scale=1'>";
  html += "<meta charset='UTF-8'>";
  html += "<style>body { font-family: Arial, sans-serif; margin: 20px; background-color: #f0f0f0; }";
  html += ".container { max-width: 800px; margin: 0 auto; background: white; padding: 20px; border-radius: 10px; }";
  html += ".data-section { background: #f8f8f8; padding: 15px; margin: 15px 0; border-radius: 5px; border-left: 4px solid #2196F3; }";
  html += "pre { white-space: pre-wrap; background: #f0f0f0; padding: 10px; border-radius: 5px; }";
  html += "</style></head><body>";
  
  html += "<div class='container'>";
  html += "<h1>📊 Просмотр данных Firebase</h1>";
  html += "<a href='/'>← Назад на панель</a>";
  
  // Latest reading
  html += "<div class='data-section'>";
  html += "<h2>Последние показания</h2>";
  if (Firebase.getJSON(firebaseData, FB_LATEST_READING)) {
    String jsonStr;
    firebaseData.jsonObject().toString(jsonStr, true);
    html += "<pre>" + jsonStr + "</pre>";
  } else {
    html += "<p>Нет доступных данных</p>";
  }
  html += "</div>";
  
  // AI Advice
  html += "<div class='data-section'>";
  html += "<h2>Советы ИИ</h2>";
  if (Firebase.getJSON(firebaseData, FB_AI_ADVICE)) {
    String jsonStr;
    firebaseData.jsonObject().toString(jsonStr, true);
    html += "<pre>" + jsonStr + "</pre>";
  } else {
    html += "<p>Нет данных по советам</p>";
  }
  html += "</div>";
  
  // Daily summary
  html += "<div class='data-section'>";
  html += "<h2>Сводка за сегодня</h2>";
  
  time_t now = timeClient.getEpochTime();
  struct tm * timeinfo;
  timeinfo = localtime(&now);
  
  char dateStr[11];
  sprintf(dateStr, "%04d-%02d-%02d", timeinfo->tm_year + 1900, timeinfo->tm_mon + 1, timeinfo->tm_mday);
  String today = String(dateStr);
  
  if (Firebase.getJSON(firebaseData, FB_DAILY_SUMMARY + "/" + today)) {
    String jsonStr;
    firebaseData.jsonObject().toString(jsonStr, true);
    html += "<pre>" + jsonStr + "</pre>";
  } else {
    html += "<p>Нет сводки за сегодня</p>";
  }
  html += "</div>";
  
  html += "</div></body></html>";
  server.send(200, "text/html", html);
}

String translateQuality(String quality) {
  if (quality == "Good") return "Хорошее";
  else if (quality == "Normal") return "Нормальное";
  else if (quality == "Moderate") return "Среднее";
  else if (quality == "Bad") return "Плохое";
  return quality;
}

String evaluateAir(int value) {
  if (value < 200) return "Good";
  else if (value < 400) return "Normal";
  else if (value < 600) return "Moderate";
  else return "Bad";
}

String generateAdvice(float temp, float humidity, int mq135, String quality) {
  String advice = "";
  
  // Temperature advice
  if (temp < 18) {
    advice += "🌡️ Температура низкая (" + String(temp, 1) + "°C). Рекомендуется включить отопление для комфорта. ";
  } else if (temp > 26) {
    advice += "🌡️ Температура высокая (" + String(temp, 1) + "°C). Рекомендуется охлаждение или вентиляция. ";
  } else {
    advice += "🌡️ Температура оптимальная (" + String(temp, 1) + "°C). ";
  }
  
  // Humidity advice
  if (humidity < 30) {
    advice += "💧 Влажность низкая (" + String(humidity, 1) + "%). Используйте увлажнитель для предотвращения проблем с сухим воздухом. ";
  } else if (humidity > 60) {
    advice += "💧 Влажность высокая (" + String(humidity, 1) + "%). Используйте осушитель для предотвращения роста плесени. ";
  } else {
    advice += "💧 Влажность в хорошем диапазоне (" + String(humidity, 1) + "%). ";
  }
  
  // Air quality advice
  if (quality == "Good") {
    advice += "🌿 Отличное качество воздуха! Продолжайте текущие методы вентиляции.";
  } else if (quality == "Normal") {
    advice += "🔄 Качество воздуха приемлемое. Рекомендуется открыть окна для циркуляции свежего воздуха.";
  } else if (quality == "Moderate") {
    advice += "⚠️ Качество воздуха требует внимания. Увеличьте вентиляцию и проверьте источники загрязнения.";
  } else {
    advice += "🚨 Обнаружено плохое качество воздуха! Требуются немедленные действия: откройте все окна, используйте воздухоочиститель, проверьте наличие утечек газа или горящих материалов.";
  }
  
  return advice;
}

String analyzeTrends() {
  String trends = "";
  int validReadings = 0;
  float avgTemp = 0, avgHumidity = 0, avgMQ135 = 0;
  
  // Calculate averages
  for(int i = 0; i < 24; i++) {
    if(recentReadings[i].timestamp > 0) {
      avgTemp += recentReadings[i].temperature;
      avgHumidity += recentReadings[i].humidity;
      avgMQ135 += recentReadings[i].mq135Value;
      validReadings++;
    }
  }
  
  if(validReadings < 3) {
    return "Недостаточно данных для анализа тенденций. Требуется не менее 3 показаний.";
  }
  
  avgTemp /= validReadings;
  avgHumidity /= validReadings;
  avgMQ135 /= validReadings;
  
  // Recent vs average comparison
  float currentTemp = dht.readTemperature();
  float currentHumidity = dht.readHumidity();
  int currentMQ135 = analogRead(A0);
  
  trends += "📈 На основе " + String(validReadings) + " недавних показаний: ";
  
  if(currentTemp > avgTemp + 2) {
    trends += "Тенденция к повышению температуры. ";
  } else if(currentTemp < avgTemp - 2) {
    trends += "Тенденция к понижению температуры. ";
  }
  
  if(currentHumidity > avgHumidity + 10) {
    trends += "Влажность повышается. ";
  } else if(currentHumidity < avgHumidity - 10) {
    trends += "Влажность понижается. ";
  }
  
  if(currentMQ135 > avgMQ135 + 50) {
    trends += "Качество воздуха ухудшается. ";
  } else if(currentMQ135 < avgMQ135 - 50) {
    trends += "Качество воздуха улучшается. ";
  }
  
  if(trends.length() < 50) {
    trends += "Условия стабильны.";
  }
  
  return trends;
}

// Helper function to convert quality to CSS class name
String qualityToClassName(String quality) {
  if (quality == "Good") return "good";
  else if (quality == "Normal") return "normal";
  else if (quality == "Moderate") return "moderate";
  else if (quality == "Bad") return "bad";
  return "normal";
}