/*
 * E220_WOR_Receiver_ESP32S3.ino
 * Dual-Core optimized for ESP32-S3 (ESP32-S3-WROOM-1-N16R8)
 * 01/11/2026
 * 
 * ESP32-S3 specific features:
 * - USB CDC Serial (not UART0)
 * - Different GPIO assignments
 * - Enhanced deep sleep with RTC
 * 
 * GPIO Assignments for ESP32-S3:
 * E220 M0  -> GPIO 5
 * E220 M1  -> GPIO 6
 * E220 TX  -> GPIO 17 (RX2)
 * E220 RX  -> GPIO 18 (TX2)
 * E220 AUX -> GPIO 7 (EXT0 wake)
 * KY002S Trigger -> GPIO 8
 * KY002S Status  -> GPIO 9
 */

#include <Arduino.h>
#include "LoRa_E220.h"
#include "driver/rtc_io.h"

#define FREQUENCY_915
#define CHANNEL 23
#define MY_ADDRESS 3
#define TRANSMITTER_ADDRESS 2

// Hardware Pins - ESP32-S3
#define RXD2 17
#define TXD2 18
#define M0_PIN GPIO_NUM_5
#define M1_PIN GPIO_NUM_6
#define AUX_PIN GPIO_NUM_7  // EXT0 wake source
#define KY002S_TRIGGER 8
#define KY002S_STATUS 9

LoRa_E220 e220ttl(&Serial2, AUX_PIN, M0_PIN, M1_PIN);

const int MAX_dateTime_LENGTH = 40;

struct Message {
  int32_t switchData;
  char dateTime[40];
} __attribute__((packed));

// Cross-core communication
volatile bool inboxReady = false;
Message inbox;
const uint16_t PULSE_MS = 200;

RTC_DATA_ATTR int bootCount = 0;

// Task handles
TaskHandle_t commTaskHandle = NULL;
TaskHandle_t logicTaskHandle = NULL;

// Forward declarations
void enterDeepSleep();
void commTask(void* parameter);
void logicTask(void* parameter);

// ================================================================
// Wait for AUX
// ================================================================
void waitForAux(int timeout = 5000) {
  uint32_t start = millis();
  while (digitalRead(AUX_PIN) == LOW && (millis() - start < timeout)) {
    vTaskDelay(pdMS_TO_TICKS(10));
  }
}

// ================================================================
// Setup
// ================================================================
void setup() {
  // ESP32-S3 uses USB CDC for Serial
  Serial.begin(115200);
  delay(1000);  // Give USB time to enumerate
  
  bootCount++;
  
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR Receiver (ESP32-S3)         ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Boot #");
  Serial.println(bootCount);

  // Initialize pins
  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  pinMode(KY002S_TRIGGER, OUTPUT);
  pinMode(KY002S_STATUS, INPUT);
  digitalWrite(KY002S_TRIGGER, LOW);

  // Check wake cause
  esp_sleep_wakeup_cause_t cause = esp_sleep_get_wakeup_cause();

  if (cause == ESP_SLEEP_WAKEUP_EXT0) {
    // =============================================
    // WOKE FROM WOR PREAMBLE
    // =============================================
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║   WOR WAKE-UP DETECTED!                ║");
    Serial.println("╚════════════════════════════════════════╝\n");

    // Release GPIO holds
    gpio_hold_dis(M0_PIN);
    gpio_hold_dis(M1_PIN);
    gpio_deep_sleep_hold_dis();

    // Initialize UART and E220
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    vTaskDelay(pdMS_TO_TICKS(100));

    e220ttl.begin();
    vTaskDelay(pdMS_TO_TICKS(100));

    // Switch to NORMAL mode
    Serial.println("Switching to NORMAL mode...");
    e220ttl.setMode(MODE_0_NORMAL);
    vTaskDelay(pdMS_TO_TICKS(100));
    waitForAux();
    
    Serial.println("Radio ready. Launching tasks...\n");

    // Launch dual-core tasks
    xTaskCreatePinnedToCore(commTask, "CommTask", 4096, NULL, 2, &commTaskHandle, 0);
    xTaskCreatePinnedToCore(logicTask, "LogicTask", 4096, NULL, 1, &logicTaskHandle, 1);

  } else {
    // =============================================
    // INITIAL POWER-ON
    // =============================================
    Serial.println("Power-on wake. Initializing...\n");

    // Initialize UART and E220
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
      Configuration config = *(Configuration*)c.data;
      Serial.print("Address: 0x");
      Serial.println(config.ADDL, HEX);
      Serial.print("Channel: ");
      Serial.println(config.CHAN);
      Serial.print("WOR Period: ");
      Serial.println(config.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
    }
    c.close();

    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║   Entering WOR Sleep Mode              ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    
    delay(1000);
    enterDeepSleep();
  }
}

// ================================================================
// Core 0: Communication Task
// ================================================================
void commTask(void* parameter) {
  Serial.print("CommTask running on core ");
  Serial.println(xPortGetCoreID());

  Serial.println("Waiting for data message...");

  unsigned long startTime = millis();
  int attempts = 0;
  bool dataReceived = false;

  // Wait up to 12 seconds for data
  while (attempts < 120 && !dataReceived) {
    if (e220ttl.available() > 0) {
      dataReceived = true;
      break;
    }
    
    vTaskDelay(pdMS_TO_TICKS(100));
    attempts++;

    if (attempts % 10 == 0) {
      Serial.print("  Still waiting... (");
      Serial.print(attempts / 10);
      Serial.println(" seconds)");
    }
  }

  if (dataReceived || e220ttl.available() > 0) {
    Serial.println("\n>>> DATA DETECTED <<<");
    
    ResponseStructContainer rsc = e220ttl.receiveMessageRSSI(sizeof(Message));

    if (rsc.status.code == 1 && rsc.data != nullptr) {
      Message* msgPtr = (Message*)rsc.data;

      Serial.println("╔════════════════════════════════════════╗");
      Serial.println("║        MESSAGE RECEIVED!               ║");
      Serial.println("╚════════════════════════════════════════╝");
      Serial.print("Switch Data: ");
      Serial.println(msgPtr->switchData);
      Serial.print("Timestamp:   ");
      Serial.println(msgPtr->dateTime);
      Serial.print("RSSI:        ");
      Serial.print(rsc.rssi);
      Serial.println(" dBm");
      Serial.print("Receive time: ");
      Serial.print(millis() - startTime);
      Serial.println("ms\n");

      // Safely copy to inbox
      memset(&inbox, 0, sizeof(Message));
      memcpy(&inbox, rsc.data, sizeof(Message));
      inbox.dateTime[MAX_dateTime_LENGTH - 1] = '\0';

      inboxReady = true;

    } else {
      Serial.println("✗ Receive error");
    }

    rsc.close();

  } else {
    Serial.println("\n⚠️  No data received in 12 seconds");
  }

  vTaskDelete(NULL);
}

// ================================================================
// Core 1: Logic Task
// ================================================================
void logicTask(void* parameter) {
  Serial.print("LogicTask running on core ");
  Serial.println(xPortGetCoreID());

  // Wait for data from Core 0
  for (int i = 0; i < 150; i++) {
    if (inboxReady) {
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (inboxReady) {
    inboxReady = false;
    Message msg = inbox;

    bool isCurrentlyOn = (digitalRead(KY002S_STATUS) == HIGH);

    Serial.println("\n--- Processing Command ---");
    Serial.print("Current state: ");
    Serial.println(isCurrentlyOn ? "ON" : "OFF");
    Serial.print("Command: ");
    Serial.println(msg.switchData == 1 ? "TURN ON" : "TURN OFF");

    // Toggle if needed
    if ((msg.switchData == 1 && !isCurrentlyOn) || (msg.switchData == 2 && isCurrentlyOn)) {
      Serial.println(">>> Toggling KY002S switch <<<");
      digitalWrite(KY002S_TRIGGER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(PULSE_MS));
      digitalWrite(KY002S_TRIGGER, LOW);
      Serial.println("✓ Switch toggled");
    } else {
      Serial.println("ℹ️  Already in requested state");
    }

    // Send acknowledgment
    Serial.println("\nSending ACK...");
    waitForAux();
    ResponseStatus rs = e220ttl.sendFixedMessage(0, TRANSMITTER_ADDRESS, CHANNEL, "ACK");
    Serial.print("ACK status: ");
    Serial.println(rs.getResponseDescription());
    waitForAux();
  }

  Serial.println("\n--- Processing Complete ---");
  vTaskDelay(pdMS_TO_TICKS(500));

  enterDeepSleep();
  vTaskDelete(NULL);
}

// ================================================================
// Power Management
// ================================================================
void enterDeepSleep() {
  Serial.println(">>> Entering WOR Sleep Mode <<<");
  Serial.flush();

  // Set WOR Receiver mode pins
  digitalWrite(M0_PIN, LOW);   // M0 = LOW
  digitalWrite(M1_PIN, HIGH);  // M1 = HIGH
  vTaskDelay(pdMS_TO_TICKS(100));
  waitForAux();

  // Hold pins during sleep
  gpio_hold_en(M0_PIN);
  gpio_hold_en(M1_PIN);
  gpio_deep_sleep_hold_en();

  // Enable wake on AUX LOW
  esp_sleep_enable_ext0_wakeup(AUX_PIN, 0);

  Serial.println("Waiting for WOR preamble...\n");
  Serial.flush();
  
  esp_deep_sleep_start();
}

void loop() {
  vTaskDelay(pdMS_TO_TICKS(1000));
}