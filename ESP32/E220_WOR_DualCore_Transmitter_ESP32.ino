/*
 * E220_WOR_Transmitter_Final.ino
 * Fixed addressing: TX=0x02, RX=0x03
 * 01/09/2026
 * 
 * PREREQUISITES:
 * Both modules must be configured first using E220_WOR_Configurator.ino
 * - Transmitter: Address 0x02
 * - Receiver: Address 0x03
 * - Both: Same WOR period (2000ms), Fixed Transmission enabled
 *
 * E220 Wiring (ESP32):
 * M0  -> GPIO 21
 * M1  -> GPIO 19
 * TX  -> GPIO 16 (RX2)
 * RX  -> GPIO 17 (TX2)
 * AUX -> GPIO 15
 * VCC -> 3.3V
 * GND -> GND
 */

#define MY_ADDRESS_ADDL 2        // THIS transmitter's address
#define RECEIVER_ADDRESS_ADDL 3  // Send TO this address
#define FREQUENCY_915
#define CHANNEL 23

#include "Arduino.h"
#include "LoRa_E220.h"
#include "WiFi.h"
#include <WiFiUdp.h>
#include <time.h>
#include <Ticker.h>
#include <AsyncTCP.h>
#include "ESPAsyncWebServer.h"

#include "index7.h"

// ---------------------------
// E220 Setup
// ---------------------------
LoRa_E220 e220ttl(&Serial2, 15, 21, 19);

#define RXD2 16
#define TXD2 17
#define M0_PIN 21
#define M1_PIN 19
#define AUX_PIN 15

#define TRIGGER 32     // KY002S MOSFET Bi-Stable Switch (drive)
#define KY002S_PIN 33  // KY002S MOSFET Bi-Stable Switch (sense)
#define ALERT 4        // ina226 Battery Monitor

// ---------------------------
// Network
// ---------------------------
const char *ssid = "R2D2";
const char *password = "Sky7388500";

AsyncWebServer server(80);

// ---------------------------
// Message Structure
// ---------------------------
const int MAX_dateTime_LENGTH = 40;
char time_output[MAX_dateTime_LENGTH];

struct Message {
  int32_t switchData;
  char dateTime[40];
} __attribute__((packed));

Message outgoing;

// ---------------------------
// State Variables
// ---------------------------
volatile bool countdownExpired = false;
volatile bool sendRequested = false;
int sendValue = 0;  // NEW: Store which value to send (1 or 2)
int needAnotherCountdown = 0;
int pulseDuration = 100;

String linkAddress = "192.168.12.27:80";

// NTP Setup
WiFiUDP udp;
const int udpPort = 1157;
const char *udpAddress1 = "pool.ntp.org";
const char *udpAddress2 = "time.nist.gov";
#define TZ "EST+5EDT,M3.2.0/2,M11.1.0/2"

// Tickers
Ticker onceTick;

// ---------------------------
// ISR: Countdown Timer
// ---------------------------
void IRAM_ATTR countdownTrigger() {
  countdownExpired = true;
}

// ---------------------------
// Utility: Wait for AUX HIGH
// ---------------------------
void waitForAux(int timeout = 5000) {
  uint32_t start = millis();
  while (digitalRead(AUX_PIN) == LOW && (millis() - start < timeout)) {
    delay(10);
    yield();  // Feed watchdog
  }
  if (digitalRead(AUX_PIN) == LOW) {
    Serial.println("  ⚠️  AUX timeout!");
  }
}

// ---------------------------
// Get formatted time string
// ---------------------------
String get_time() {
  time_t now;
  time(&now);
  strftime(time_output, MAX_dateTime_LENGTH, "%a  %m/%d/%y   %T", localtime(&now));
  return String(time_output);
}

// ---------------------------
// Configure NTP Time
// ---------------------------
void configTimeCustom() {
  configTime(0, 0, udpAddress1, udpAddress2);
  setenv("TZ", TZ, 1);
  tzset();

  Serial.print("Waiting for NTP time sync");
  while (time(nullptr) < 100000ul) {
    Serial.print(".");
    delay(1000);
  }
  Serial.println("\nSystem Time synchronized");
  
  get_time();
  Serial.println(time_output);
}

// ---------------------------
// WiFi Setup with Static IP
// ---------------------------
void wifi_Start() {
  IPAddress local_IP(192, 168, 12, 27);
  IPAddress gateway(192, 168, 12, 1);
  IPAddress subnet(255, 255, 255, 0);
  IPAddress primaryDNS(8, 8, 8, 8);
  IPAddress secondaryDNS(8, 8, 4, 4);

  if (!WiFi.config(local_IP, gateway, subnet, primaryDNS, secondaryDNS)) {
    Serial.println("Static IP configuration failed");
  }

  WiFi.begin(ssid, password);

  Serial.print("Connecting to WiFi");
  while (WiFi.status() != WL_CONNECTED) {
    delay(500);
    Serial.print(".");
  }

  Serial.println("\nWiFi Connected!");
  Serial.print("IP address: ");
  Serial.println(WiFi.localIP());
}

// ---------------------------
// Initialize E220 Radio
// ---------------------------
void initRadio() {
  Serial.println("Initializing E220 radio...");

  pinMode(AUX_PIN, INPUT);
  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(100);

  e220ttl.begin();
  delay(100);

  // Start in NORMAL mode
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();

  // Verify configuration
  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code == 1) {
    Configuration config = *(Configuration*) c.data;
    
    Serial.println("\n--- E220 Configuration ---");
    Serial.print("Address: 0x");
    Serial.println(config.ADDL, HEX);
    Serial.print("Channel: ");
    Serial.println(config.CHAN);
    Serial.print("Fixed TX: ");
    Serial.println(config.TRANSMISSION_MODE.fixedTransmission == FT_FIXED_TRANSMISSION ? "YES" : "NO");
    Serial.print("WOR Period: ");
    Serial.println(config.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
    
    if (config.ADDL != MY_ADDRESS_ADDL) {
      Serial.println("\n⚠️  WARNING: Module address doesn't match!");
      Serial.print("   Expected: 0x");
      Serial.println(MY_ADDRESS_ADDL, HEX);
      Serial.print("   Got: 0x");
      Serial.println(config.ADDL, HEX);
      Serial.println("   Use E220_WOR_Configurator.ino to fix this!");
    }
    
    if (config.TRANSMISSION_MODE.fixedTransmission != FT_FIXED_TRANSMISSION) {
      Serial.println("\n⚠️  WARNING: Fixed Transmission not enabled!");
      Serial.println("   Use E220_WOR_Configurator.ino to enable it!");
    }
    
    Serial.println("-------------------------\n");
  }
  c.close();

  Serial.println("E220 Ready\n");
}

// ---------------------------
// Send WOR Preamble
// Wakes the receiver from WOR sleep mode
// ---------------------------
void sendPreamble() {
  unsigned long startTime = millis();
  
  Serial.println("\n=== STEP 1: WOR PREAMBLE ===");
  Serial.print("  From: 0x");
  Serial.println(MY_ADDRESS_ADDL, HEX);
  Serial.print("  To:   0x");
  Serial.println(RECEIVER_ADDRESS_ADDL, HEX);
  Serial.print("  Ch:   ");
  Serial.println(CHANNEL);
  
  waitForAux();
  
  // Switch to WOR Transmitter mode
  unsigned long modeStart = millis();
  Serial.println("  Setting WOR TX mode...");
  e220ttl.setMode(MODE_1_WOR_TRANSMITTER);
  delay(100);
  waitForAux();
  Serial.print("  Mode switch took: ");
  Serial.print(millis() - modeStart);
  Serial.println("ms");
  
  // Send short preamble - the WOR preamble is what wakes receiver
  unsigned long sendStart = millis();
  Serial.println("  Sending preamble...");
  ResponseStatus rs = e220ttl.sendFixedMessage(0, RECEIVER_ADDRESS_ADDL, CHANNEL, "WAKE");
  
  Serial.print("  Status: ");
  Serial.println(rs.getResponseDescription());
  Serial.print("  Preamble transmission took: ");
  Serial.print(millis() - sendStart);
  Serial.println("ms");
  
  waitForAux();

  // Critical: Wait for receiver to wake and be ready
  // WOR wake-up sequence:
  // 1. Receiver detects preamble (~100-500ms)
  // 2. Receiver wakes from sleep (~500-1000ms)  
  // 3. Receiver switches to normal mode (~100ms)
  // 4. Receiver ready to receive data
  Serial.println("  Waiting for RX wake...");
  
  // 3 second wait with progress and watchdog feeding
  for (int i = 3; i > 0; i--) {
    Serial.print("    ");
    Serial.print(i);
    Serial.println(" seconds...");
    
    // Break into smaller delays to feed watchdog
    for (int j = 0; j < 10; j++) {
      delay(100);
      yield();  // Feed the watchdog
    }
  }
  
  Serial.print("=== PREAMBLE COMPLETE (Total: ");
  Serial.print(millis() - startTime);
  Serial.println("ms) ===\n");
}

// ---------------------------
// Send Data Message
// Sends actual payload after receiver is awake
// ---------------------------
void sendDataMessage(int switchValue) {
  unsigned long startTime = millis();
  
  Serial.println("=== STEP 2: DATA MESSAGE ===");
  Serial.print("  Switch value: ");
  Serial.println(switchValue);

  waitForAux();

  // Prepare message
  unsigned long prepStart = millis();
  memset(&outgoing, 0, sizeof(Message));
  outgoing.switchData = switchValue;
  
  get_time();
  strncpy(outgoing.dateTime, time_output, MAX_dateTime_LENGTH - 1);
  outgoing.dateTime[MAX_dateTime_LENGTH - 1] = '\0';

  Serial.print("  Timestamp: ");
  Serial.println(outgoing.dateTime);
  Serial.print("  Message prep took: ");
  Serial.print(millis() - prepStart);
  Serial.println("ms");

  // Send message (still in WOR TX mode)
  unsigned long sendStart = millis();
  Serial.println("  Sending data...");
  ResponseStatus rs = e220ttl.sendFixedMessage(0, RECEIVER_ADDRESS_ADDL, CHANNEL,
                                               &outgoing, sizeof(Message));
  
  Serial.print("  Status: ");
  Serial.println(rs.getResponseDescription());
  Serial.print("  Data transmission took: ");
  Serial.print(millis() - sendStart);
  Serial.println("ms");

  waitForAux();

  // Return to NORMAL mode
  unsigned long modeStart = millis();
  Serial.println("  Returning to normal mode...");
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  Serial.print("  Mode switch took: ");
  Serial.print(millis() - modeStart);
  Serial.println("ms");

  Serial.print("=== DATA COMPLETE (Total: ");
  Serial.print(millis() - startTime);
  Serial.println("ms) ===\n");
}

// ---------------------------
// Main WOR Send Function
// ---------------------------
void performWORSend(int switchValue) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     WOR TRANSMISSION STARTING          ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  if (switchValue == 1) {
    Serial.println("Event: Battery Switch ON");
  } else if (switchValue == 2) {
    Serial.println("Event: Battery Switch OFF");
  }
  
  // Two-step WOR process:
  // 1. Send preamble to wake receiver
  sendPreamble();
  
  // 2. Send actual data message
  sendDataMessage(switchValue);
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║     WOR TRANSMISSION COMPLETE          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
}

// ---------------------------
// Template processor for web page
// ---------------------------
String processor7(const String &var) {
  if (var == F("LINK"))
    return linkAddress;
  return String();
}

// ---------------------------
// Setup
// ---------------------------
void setup() {
  Serial.begin(115200);
  delay(500);

  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR Transmitter v2.0            ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Initialize pins
  pinMode(TRIGGER, OUTPUT);
  pinMode(KY002S_PIN, INPUT);
  pinMode(ALERT, INPUT);
  digitalWrite(TRIGGER, LOW);

  // Start WiFi
  wifi_Start();

  // Sync time
  configTimeCustom();

  // Initialize radio
  initRadio();

  // Check KY002S switch state
  int value = digitalRead(KY002S_PIN);
  if (value == HIGH) {
    Serial.println("Toggling KY002S switch...");
    digitalWrite(TRIGGER, HIGH);
    delay(pulseDuration);
    digitalWrite(TRIGGER, LOW);
  }

  // Setup web server route
  server.on("/relay", HTTP_GET, [](AsyncWebServerRequest *request) {
    request->send_P(200, PSTR("text/html"), HTML7, processor7);
    
    // Set flag for switchData = 1 (Turn ON)
    sendValue = 1;
    sendRequested = true;
    
    // Start 60-second countdown for auto-shutdown
    onceTick.once(60, countdownTrigger);
  });

  server.begin();
  
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║         SYSTEM READY                   ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("URL: http://192.168.12.27/relay\n");
  
  Serial.println("RECEIVER MUST BE:");
  Serial.println("  • Address: 0x03");
  Serial.println("  • Mode: MODE_2_WOR_RECEIVER");
  Serial.println("  • WOR Period: 2000ms");
  Serial.println("  • Fixed TX: Enabled");
  Serial.println("  • AUX: Monitored for wake signal\n");
}

// ---------------------------
// Main Loop
// ---------------------------
void loop() {
  // Check if web request triggered a send
  if (sendRequested) {
    sendRequested = false;
    
    Serial.println("\n🌐 WEB REQUEST RECEIVED");
    performWORSend(sendValue);
  }
  
  // Check if 60-second countdown expired
  if (countdownExpired) {
    countdownExpired = false;
    
    Serial.println("\n⏰ COUNTDOWN EXPIRED - AUTO SHUTDOWN");
    
    // Send switchData = 2 (Turn OFF)
    sendValue = 2;
    performWORSend(sendValue);
  }
  
  delay(10);
}