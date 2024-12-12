#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <LittleFS.h>
#include <FS.h>
#include <WiFiManager.h>
#include <ESP8266mDNS.h>

#define ECHO_PIN D2
#define TRIG_PIN D1
#define LED_RE D D5
#define LED_YELLOW D6
#define LED_GREEN D7
#define BUZZER_PIN D4      
#define BUZZER_PIN_2 D3   

const long interval = 3000; // Interval for distance measurements
const unsigned long yellowBuzzDuration = 15000; // 15 seconds for yellow buzzer
const unsigned long redBuzzDuration = 15000;    // 15 seconds for red buzzer
const unsigned long cooldownDuration = 1800000; // 30 minutes

unsigned long lastYellowBuzzTime = 0;
unsigned long lastRedBuzzTime = 0;
bool yellowInCooldown = false;
bool redInCooldown = false;
bool sensorActive = false;

ESP8266WebServer server(80); // Web server instance

enum Zone { SAFE, WARNING, DANGER };

// Function declarations
void handleIndex();
void handleDistance();
void handleAdmin();
void handleToggleSensor();
float readDistance();
void manageCooldowns(unsigned long currentMillis);
Zone determineZone(float distance);
void handleZone(Zone zone, unsigned long currentMillis);
void handleBuzzer(unsigned long currentMillis, unsigned long buzzDuration, bool& inCooldown, unsigned long& lastBuzzTime, int buzzerPin);
void startFireAlarm(int buzzerPin);
void startSiren(int buzzerPin);

void setup() {
  Serial.begin(115200);
  pinMode(TRIG_PIN, OUTPUT);
  pinMode(ECHO_PIN, INPUT);
  pinMode(LED_RED, OUTPUT);
  pinMode(LED_YELLOW, OUTPUT);
  pinMode(LED_GREEN, OUTPUT);
  pinMode(BUZZER_PIN, OUTPUT);
  pinMode(BUZZER_PIN_2, OUTPUT);

  // Initialize WiFiManager
  WiFiManager wifiManager;
  wifiManager.setAPCallback([](WiFiManager *myWiFiManager) {
    Serial.println("Entered AP mode, waiting for Wi-Fi configuration...");
  });

  if (!wifiManager.autoConnect("ESP8266_AP")) {
    Serial.println("Failed to connect and hit timeout");
    ESP.reset();  // Reset ESP8266 if connection fails
    delay(1000);
  }

  Serial.println("Connected to Wi-Fi");
  Serial.print("IP Address: ");
  Serial.println(WiFi.localIP());

  if (!SPIFFS.begin()) {
    Serial.println("Failed to mount file system");
    return;
  }

  if (!MDNS.begin("flood-monitoring")) {
    Serial.println("Error starting mDNS");
  } else {
    Serial.println("mDNS started");
    MDNS.addService("http", "tcp", 80); // Advertise HTTP service
  }

  server.on("/", handleIndex);
  server.on("/distance", handleDistance);
  server.on("/admin.html", handleAdmin);
  server.on("/toggle_sensor", handleToggleSensor);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  static unsigned long previousMillis = 0;
  unsigned long currentMillis = millis();

  server.handleClient();
  MDNS.update();  // Update mDNS

  if (sensorActive && currentMillis - previousMillis >= interval) {
    previousMillis = currentMillis;
    float distance = readDistance();
    manageCooldowns(currentMillis);
    Zone zone = determineZone(distance);
    handleZone(zone, currentMillis);
  }
}

float readDistance() {
  digitalWrite(TRIG_PIN, LOW);
  delayMicroseconds(2);
  digitalWrite(TRIG_PIN, HIGH);
  delayMicroseconds(10);
  digitalWrite(TRIG_PIN, LOW);
  int duration = pulseIn(ECHO_PIN, HIGH);
  return duration * 0.034 / 2; // Distance in cm
}

void manageCooldowns(unsigned long currentMillis) {
  if (yellowInCooldown && currentMillis - lastYellowBuzzTime >= cooldownDuration) {
    yellowInCooldown = false;
  }
  if (redInCooldown && currentMillis - lastRedBuzzTime >= cooldownDuration) {
    redInCooldown = false;
  }
}

Zone determineZone(float distance) {
  if (distance <= 10) return DANGER;
  if (distance <= 20) return WARNING;
  return SAFE;
}

void handleZone(Zone zone, unsigned long currentMillis) {
  switch (zone) {
    case SAFE:
      digitalWrite(LED_GREEN, HIGH);
      digitalWrite(LED_YELLOW, LOW);
      digitalWrite(LED_RED, LOW);
      noTone(BUZZER_PIN);    // Turn off both buzzers if safe
      noTone(BUZZER_PIN_2);
      break;
    case WARNING:
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_YELLOW, HIGH);
      digitalWrite(LED_RED, LOW);
      handleBuzzer(currentMillis, yellowBuzzDuration, yellowInCooldown, lastYellowBuzzTime, BUZZER_PIN); // Yellow buzzer
      break;
    case DANGER:
      digitalWrite(LED_GREEN, LOW);
      digitalWrite(LED_YELLOW, LOW);
      digitalWrite(LED_RED, HIGH);
      handleBuzzer(currentMillis, redBuzzDuration, redInCooldown, lastRedBuzzTime, BUZZER_PIN_2); // Red buzzer
      break;
  }
}

void handleBuzzer(unsigned long currentMillis, unsigned long buzzDuration, bool& inCooldown, unsigned long& lastBuzzTime, int buzzerPin) {
  if (!inCooldown) {
    lastBuzzTime = currentMillis;
    inCooldown = true;
  }
  if (currentMillis - lastBuzzTime < buzzDuration) {
    if (buzzerPin == BUZZER_PIN) { // Yellow zone
      startFireAlarm(BUZZER_PIN); // Fire alarm for yellow
    } else if (buzzerPin == BUZZER_PIN_2) { // Red zone
      startSiren(BUZZER_PIN_2); // Siren for red
    }
  } else {
    noTone(buzzerPin); // Stop the buzzer after the cooldown
  }
}

void startFireAlarm(int buzzerPin) {
  static unsigned long lastToggleTime = 0;
  unsigned long currentMillis = millis();
  if (currentMillis - lastToggleTime >= 200) {
    lastToggleTime = currentMillis;
    static bool toggle = false;
    tone(buzzerPin, toggle ? 1500 : 1200); // Toggle between 1500Hz and 1200Hz
    toggle = !toggle;
  }
}

void startSiren(int buzzerPin) {
  for (int freq = 500; freq <= 1000; freq += 20) {
    tone(buzzerPin, freq); // Play a rising frequency
    delay(30);
  }
  for (int freq = 1000; freq >= 500; freq -= 20) {
    tone(buzzerPin, freq); // Play a falling frequency
    delay(30);
  }
}

void handleIndex() {
  File file = SPIFFS.open("/index.html", "r");
  if (!file) {
    Serial.println("Error opening /index.html file");
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleDistance() {
  if (!sensorActive) {
    server.send(200, "application/json", "{\"error\": \"Sensor is off\"}");
    return;
  }
  float distance = readDistance();
  Zone zone = determineZone(distance);

  String ledStatus = (zone == SAFE) ? "green" : (zone == WARNING) ? "yellow" : "red";
  String buzzerStatus = (digitalRead(BUZZER_PIN) ? "yellow" : (digitalRead(BUZZER_PIN_2) ? "red" : "off"));
  
  String jsonResponse = String("{\"distance\":") + String(distance) +
                        String(", \"led\": \"") + ledStatus +
                        String("\", \"buzzer\": \"") + buzzerStatus + "\"}";
  server.send(200, "application/json", jsonResponse);
}

void handleAdmin() {
  File file = SPIFFS.open("/admin.html", "r");
  if (!file) {
    Serial.println("Error opening /admin.html file");
    server.send(404, "text/plain", "File Not Found");
    return;
  }
  server.streamFile(file, "text/html");
  file.close();
}

void handleToggleSensor() {
  sensorActive = !sensorActive; // Toggle sensor state
  String status = sensorActive ? "Sensor activated" : "Sensor deactivated";
  server.send(200, "text/plain", status);
}
