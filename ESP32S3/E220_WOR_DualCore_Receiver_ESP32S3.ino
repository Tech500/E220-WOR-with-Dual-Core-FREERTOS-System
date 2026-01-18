/*
 * E220_WOR_DualCore_Receiver_ESP32S3.ino v6
 * 01/17/2026 @ 02:00 EST
 * ---------------------------------------------------------
 * Optimized for ESP32-S3 and E220-900T22S (LoRa)
 * Features: Dual-Core processing, EXT0 Wake-on-Radio, 
 * RTC Domain management for S3, and Strapping Pin Avoidance.
 * * GPIO Assignments for ESP32-S3:
 * E220 M0  -> GPIO 5
 * E220 M1  -> GPIO 6
 * E220 TX  -> GPIO 15 (Connects to ESP32 RX2)
 * E220 RX  -> GPIO 16 (Connects to ESP32 TX2)
 * E220 AUX -> GPIO 4  (EXT0 wake source + External Pull-up)
 * KY002S Trigger -> GPIO 8
 * KY002S Status  -> GPIO 9
 * ---------------------------------------------------------
 */

#define FREQUENCY_915
#include "LoRa_E220.h"
#include "esp_sleep.h"
#include "esp32s3/rom/rtc.h"   
#include "driver/rtc_io.h"

// Configuration Constants
#define CHANNEL 23
#define MY_ADDRESS 3
#define TRANSMITTER_ADDRESS 2
#define PULSE_MS 200

// Hardware Pins - ESP32-S3
#define RXD2 15  
#define TXD2 16  
#define M0_PIN GPIO_NUM_5
#define M1_PIN GPIO_NUM_6
#define AUX_PIN GPIO_NUM_4 
#define KY002S_TRIGGER 8
#define KY002S_STATUS 9

LoRa_E220 e220ttl(&Serial2, AUX_PIN, M0_PIN, M1_PIN);

struct Message {
  int32_t switchData;
  char dateTime[40];
} __attribute__((packed));

// Cross-core communication
volatile bool inboxReady = false;
Message inbox;
RTC_DATA_ATTR int bootCount = 0;

// Task handles
TaskHandle_t commTaskHandle = NULL;
TaskHandle_t logicTaskHandle = NULL;

// Forward declarations
void enterDeepSleep();
void commTask(void* parameter);
void logicTask(void* parameter);

// ================================================================
// Utility: Wait for AUX to go HIGH
// ================================================================
bool waitForAux(uint32_t timeout = 5000) {
    uint32_t start = millis();
    while (digitalRead(AUX_PIN) == LOW) {
        if (millis() - start >= timeout) {
            return false;   // timed out
        }
        vTaskDelay(1);
    }
    return true;            // AUX went HIGH
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
  //e220ttl.setMode(MODE_2_WOR_RECEIVER);
  //delay(100);
  //waitForAux();

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
    
    if (config.ADDL != MY_ADDRESS) {
      Serial.println("\n⚠️  WARNING: Module address doesn't match!");
      Serial.print("   Expected: 0x");
      Serial.println(MY_ADDRESS, HEX);
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

void print_reset_reason(int reason) {
    switch (reason) {
        case 1: Serial.println("POWERON_RESET"); break;     // Power-on reset
        case 3: Serial.println("SW_RESET"); break;          // Software reset
        case 4: Serial.println("OWDT_RESET"); break;        // Legacy watchdog reset
        case 5: Serial.println("DEEPSLEEP_RESET"); break;   // Deep sleep reset
        case 9: Serial.println("RTCWDT_SYS_RESET"); break;  // RTC watchdog reset
        case 15: Serial.println("RTCWDT_BROWN_OUT_RESET"); break; // Brownout reset
        case 16: Serial.println("RTCWDT_RTC_RESET"); break; // RTC watchdog reset (digital core & RTC)
        default: Serial.println("UNKNOWN_RESET");
    }
}

// ================================================================
// Setup: Entry Point
// ================================================================
void setup() {
  // 1. CRITICAL S3 FIX: Release RTC lock so GPIO 4 works for digitalRead
  rtc_gpio_deinit(AUX_PIN);
  
  Serial.begin(115200);
  while (!Serial && millis() < 2000); 
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  bootCount++;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR Dual-Core Receiver          ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Boot Count: "); Serial.println(bootCount);    // Display Configuration

  // Initialize Hardware Pins
  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  
  // 2a. Determine reset reason
  Serial.println("Reset Reason:");
  int resetReason = rtc_get_reset_reason(0); // CPU0 reset reason
  print_reset_reason(resetReason);
  
  esp_reset_reason_t reset_reason = esp_reset_reason   ();
  
  if (reset_reason == ESP_RST_POWERON) {
   
    initRadio();
    enterDeepSleep();
  } 
  else if (reset_reason == ESP_RST_DEEPSLEEP) {
    Serial.println("Waking from Deep Sleep - Quick Init");
    
    // Release GPIO holds
    gpio_hold_dis(M0_PIN);
    gpio_hold_dis(M1_PIN);
    gpio_deep_sleep_hold_dis();
    
    // Re-initialize Serial2
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    delay(100);
    
    // CRITICAL: Initialize the E220 library object
    // This sets up internal pointers/state but doesn't reconfigure the module
    e220ttl.begin();
    delay(100);
    
    // Module is already in MODE_2_WOR_RECEIVER from before sleep
    // No need to reconfigure, just ensure pins are correct
    
   
   

    // Now safe to launch tasks that call e220ttl.available()
    xTaskCreatePinnedToCore(commTask, "CommTask", 4096, NULL, 2, &commTaskHandle, 0);
    xTaskCreatePinnedToCore(logicTask, "LogicTask", 4096, NULL, 1, &logicTaskHandle, 1);
  }
}

// ================================================================
// Core 0: Communication (Receive Data)
// ================================================================
void commTask(void* parameter) {
  unsigned long startTime = millis();
  bool dataReceived = false;

  // Wait up to 12 seconds for the actual packet following preamble
  for (int i = 0; i < 120 && !dataReceived; i++) {
    if (e220ttl.available() > 0) {
      dataReceived = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (dataReceived) {
    ResponseStructContainer rsc = e220ttl.receiveMessageRSSI(sizeof(Message));
    if (rsc.status.code == 1 && rsc.data != nullptr) {
      memcpy(&inbox, rsc.data, sizeof(Message));
      inbox.dateTime[39] = '\0'; // Safety null
      inboxReady = true;
      Serial.println(">>> Message Received Successfully");
    }
    rsc.close();
  } else {
    Serial.println("⚠️ No packet followed preamble.");
  }
  vTaskDelete(NULL);
}

// ================================================================
// Core 1: Logic (Action & ACK)
// ================================================================
void logicTask(void* parameter) {
  // Wait for CommTask to finish
  for (int i = 0; i < 150 && !inboxReady; i++) {
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (inboxReady) {
    inboxReady = false;

    pinMode(KY002S_TRIGGER, OUTPUT);
    pinMode(KY002S_STATUS, INPUT);

    bool isCurrentlyOn = (digitalRead(KY002S_STATUS) == HIGH);

    // Display Requested state
    Serial.print("Requested State: ");
    Serial.println(inbox.switchData == 1 ? "ON" : "OFF");

    // Toggle logic
    if (KY002S_STATUS == LOW){
      digitalWrite(KY002S_TRIGGER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(PULSE_MS));
      digitalWrite(KY002S_TRIGGER, LOW);
    } 
    
    if(KY002S_STATUS == HIGH) {
      digitalWrite(KY002S_TRIGGER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(PULSE_MS));
      digitalWrite(KY002S_TRIGGER, LOW);
    }
      
      if (inbox.switchData == 1) {
        Serial.println("✓ Battery Power Switched ON");
      } else {
        Serial.println("✓ Battery Power Switched OFF");
      }
    } else {
      Serial.println("⚠ Already in requested state - No toggle needed");
    }

    // Send ACK
    waitForAux();
    e220ttl.sendFixedMessage(0, TRANSMITTER_ADDRESS, CHANNEL, "ACK");
    Serial.println("✓ ACK Sent to Transmitter");
    waitForAux();
  
  vTaskDelay(pdMS_TO_TICKS(500));
  //Serial.println("Entering Deep Sleep...");
  enterDeepSleep();
  vTaskDelete(NULL);
}

// ================================================================
// Power Management: S3 Optimized Deep Sleep
// ================================================================
void enterDeepSleep() {
  
  if(inbox.switchData == 1){
    Serial.println("\n>>> Awaiting countdown timer to expire...\n");
  }else if(inbox.switchData == 2){
    Serial.println("\n>>> Entering Deep Sleep...\n");
  }

  Serial.flush();

  e220ttl.setMode(MODE_2_WOR_RECEIVER);
  waitForAux();
  vTaskDelay(pdMS_TO_TICKS(100));
  
  // Initialize RTC Domain for S3
  rtc_gpio_init(AUX_PIN);
  rtc_gpio_set_direction(AUX_PIN, RTC_GPIO_MODE_INPUT_ONLY);

  // Latch Pins
  gpio_hold_en(M0_PIN);
  gpio_hold_en(M1_PIN);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup(AUX_PIN, 0); // Wake on AUX LOW
  esp_deep_sleep_start();
}

void loop() {
  // Idle
}
