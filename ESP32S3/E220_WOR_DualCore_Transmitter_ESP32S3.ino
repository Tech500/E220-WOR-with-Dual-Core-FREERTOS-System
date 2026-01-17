/*
 * E220_WOR_DualCore_Transmitter_ESP32S3.ino  v4
 * Optimized for ESP32-S3 (ESP32-S3-WROOM-1-N16R8)
 * 01/16/2026
 * 
 * ESP32-S3 specific features:
 * - USB CDC Serial (not UART0)
 * - Different GPIO assignments (avoiding conflicts)
 * - Enhanced power management
 * 
 * GPIO Assignments for ESP32-S3:
 * E220 M0  -> GPIO 5
 * E220 M1  -> GPIO 6  
 * E220 TX  -> GPIO 15 (connects to ESP32 RXD2)
 * E220 RX  -> GPIO 16 (connects to ESP32 TXD2)
 * E220 AUX -> GPIO 4  (CORRECTED - matches receiver)
 * KY002S Trigger -> GPIO 8
 * KY002S Status  -> GPIO 9
 */

#define MY_ADDRESS_ADDL 2
#define RECEIVER_ADDRESS_ADDL 3
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
// E220 Setup - ESP32-S3 GPIO
// ---------------------------
#define RXD2 16  // Serial2 RX
#define TXD2 15  // Serial2 TX
#define M0_PIN 5
#define M1_PIN 6
#define AUX_PIN 4     // Must match receiver!
#define TRIGGER 8     // KY002S MOSFET Bi-Stable Switch (drive)
#define KY002S_PIN 9  // KY002S MOSFET Bi-Stable Switch (sense)
#define ALERT 10      // ina226 Battery Monitor (optional)

LoRa_E220 e220ttl(&Serial2, AUX_PIN, M0_PIN, M1_PIN);

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
int sendValue = 0;
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
    yield();
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

  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();

  // Verify configuration
  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code == 1) {
    Configuration config = *(Configuration *)c.data;

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
    }

    Serial.println("-------------------------\n");
  }
  c.close();

  Serial.println("E220 Ready\n");
}

// ---------------------------------------------
// Send Preamble and Paylod
//----------------------------------------------
void sendWORMessage(int switchValue) {
  unsigned long startTime = millis();
  Serial.println("\n==============================");
  Serial.println("   WOR SEND SEQUENCE START");
  Serial.println("==============================");

  // ---------------------------------------
  // STEP 1: PREPARE MESSAGE PAYLOAD
  // ---------------------------------------
  Serial.println("STEP 1: Preparing data payload...");
  memset(&outgoing, 0, sizeof(Message));
  outgoing.switchData = switchValue;

  get_time();
  strncpy(outgoing.dateTime, time_output, MAX_dateTime_LENGTH - 1);
  outgoing.dateTime[MAX_dateTime_LENGTH - 1] = '\0';

  Serial.print("  Switch value: ");
  Serial.println(switchValue);
  Serial.print("  Timestamp: ");
  Serial.println(outgoing.dateTime);

  // ---------------------------------------
  // STEP 2: ENTER WOR TX MODE
  // ---------------------------------------
  Serial.println("\nSTEP 2: Entering WOR TX mode...");
  waitForAux();
  e220ttl.setMode(MODE_1_WOR_TRANSMITTER);
  delay(100);
  waitForAux();
  Serial.println("  WOR TX mode confirmed.");

  // ---------------------------------------
  // STEP 3: SEND PREAMBLE
  // ---------------------------------------
  Serial.println("\nSTEP 3: Sending WOR preamble...");
  ResponseStatus rsPre = e220ttl.sendFixedMessage(
    0, RECEIVER_ADDRESS_ADDL, CHANNEL, "WAKE");

  Serial.print("  Preamble status: ");
  Serial.println(rsPre.getResponseDescription());
  waitForAux();

  // ---------------------------------------
  // STEP 4: SWITCH BACK TO NORMAL TX MODE
  // ---------------------------------------
  Serial.println("\nSTEP 4: Switching to NORMAL TX mode...");
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  Serial.println("  Normal TX mode confirmed.");

  // ---------------------------------------
  // STEP 5: SEND ACTUAL DATA PACKET
  // ---------------------------------------
  Serial.println("\nSTEP 5: Sending data packet...");
  ResponseStatus rsData = e220ttl.sendFixedMessage(
    0, RECEIVER_ADDRESS_ADDL, CHANNEL,
    &outgoing, sizeof(Message));

  Serial.print("  Data status: ");
  Serial.println(rsData.getResponseDescription());
  waitForAux();

  // Wait for ACK from receiver
  Serial.println("Waiting for ACK...");
  unsigned long startWait = millis();
  bool ackReceived = false;

  while (millis() - startWait < 3000) {
    if (e220ttl.available() > 0) {
      ResponseContainer rc = e220ttl.receiveMessage();
      if (rc.status.code == 1) {
        String ack = rc.data;
        if (ack == "ACK") {
          Serial.println("✓ ACK received from receiver!");
          ackReceived = true;
          break;
        }
      }      
    }
    delay(50);
  }

  if (!ackReceived) {
    Serial.println("✗ No ACK received");
  }

  // ---------------------------------------
  // DONE
  // ---------------------------------------
  Serial.println("\n==============================");
  Serial.print("   WOR SEND COMPLETE (");
  Serial.print(millis() - startTime);
  Serial.println(" ms)");
  Serial.println("==============================\n");
}

// -----------------------------
// Start WOR Preamble and Payload
// -----------------------------
void performWORSend(int switchValue) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║     WOR TRANSMISSION STARTING          ║");
  Serial.println("╚════════════════════════════════════════╝");

  if (switchValue == 1) {
    Serial.println("Event: Battery Switch ON");
  } else if (switchValue == 2) {
    Serial.println("Event: Battery Switch OFF");
  }

  sendWORMessage(switchValue);

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
  // ESP32-S3 uses USB CDC for Serial
  Serial.begin(115200);
  delay(1000);  // Give USB time to enumerate

  Serial.println("\n\n╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR Transmitter (ESP32-S3) v3   ║");
  Serial.println("╚════════════════════════════════════════╝\n");

  // Initialize pins
  pinMode(TRIGGER, OUTPUT);
  pinMode(KY002S_PIN, INPUT);
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

    sendRequested = true;

    // Start 60-second countdown for auto-shutdown
    onceTick.once(60, countdownTrigger);  //120 seconds for NPPKII observations
  });

  server.begin();

  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║         SYSTEM READY                   ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.println("URL: http://192.168.12.27/relay\n");
}

// ---------------------------
// Main Loop
// ---------------------------
void loop() {
  // Check if web request triggered a send
  if (sendRequested) {
    sendRequested = false;

    Serial.println("\n🌐 WEB REQUEST RECEIVED");
    sendValue = 1;
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
