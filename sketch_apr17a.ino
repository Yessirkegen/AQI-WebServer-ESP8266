#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <DHT.h>

#define DHTPIN D2
#define DHTTYPE DHT22
DHT dht(DHTPIN, DHTTYPE);

const char* ssid = "yato";
const char* password = "123123123";

ESP8266WebServer server(80);

void setup() {
  Serial.begin(115200);
  dht.begin();
  WiFi.begin(ssid, password);
  while (WiFi.status() != WL_CONNECTED) {
    delay(1000);
    Serial.print(".");
  }

  Serial.println("");
  Serial.println("WiFi connected!");
  Serial.println(WiFi.localIP());

  server.on("/", [](){
    float t = dht.readTemperature();
    float h = dht.readHumidity();
    int mq135 = analogRead(A0);
    String quality = evaluateAir(mq135);

    String html = "<html><head><meta http-equiv='refresh' content='5'></head><body>";
    html += "<h2>Temperature: " + String(t) + " °C</h2>";
    html += "<h2>Humidity: " + String(h) + " %</h2>";
    html += "<h2>MQ135 raw: " + String(mq135) + "</h2>";
    html += "<h2>AQI " + quality + "</h2>";
    html += "</body></html>";
    
    server.send(200, "text/html", html);
  });

  server.begin();
  Serial.println("HTTP сервер запущен");
}

void loop() {
  server.handleClient();
}

String evaluateAir(int value) {
  if (value < 200) return "Good";
  else if (value < 400) return "Normal";
  else if (value < 600) return "Moderate";
  else return "Bad";
}
