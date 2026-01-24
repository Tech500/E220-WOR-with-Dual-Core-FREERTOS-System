/*
 * E220_WOR_DualCore_Receiver_ESP32S3.ino v7 - WITH TIMING
 * 01/24/2026
 * ---------------------------------------------------------
 * Added comprehensive timing instrumentation for all receiver events
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
Message inbox = {0, ""};  // <-- Initialize with zeros
//Message inbox;
RTC_DATA_ATTR int bootCount = 0;

// Task handles
TaskHandle_t commTaskHandle = NULL;
TaskHandle_t logicTaskHandle = NULL;

// Timing variables
unsigned long wakeTime = 0;
unsigned long radioReadyTime = 0;
unsigned long packetStartTime = 0;
unsigned long packetReceivedTime = 0;
unsigned long relayStartTime = 0;
unsigned long relayCompleteTime = 0;
unsigned long ackStartTime = 0;
unsigned long ackCompleteTime = 0;
unsigned long sleepPrepTime = 0;

// Forward declarations
void enterDeepSleep();
void commTask(void* parameter);
void logicTask(void* parameter);
void printTimingSummary();

// ================================================================
// Utility: Wait for AUX to go HIGH
// ================================================================
bool waitForAux(uint32_t timeout = 5000) {
    uint32_t start = millis();
    while (digitalRead(AUX_PIN) == LOW) {
        if (millis() - start >= timeout) {
            return false;
        }
        vTaskDelay(1);
    }
    return true;
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
        case 1: Serial.println("POWERON_RESET"); break;
        case 3: Serial.println("SW_RESET"); break;
        case 4: Serial.println("OWDT_RESET"); break;
        case 5: Serial.println("DEEPSLEEP_RESET"); break;
        case 9: Serial.println("RTCWDT_SYS_RESET"); break;
        case 15: Serial.println("RTCWDT_BROWN_OUT_RESET"); break;
        case 16: Serial.println("RTCWDT_RTC_RESET"); break;
        default: Serial.println("UNKNOWN_RESET");
    }
}

// ================================================================
// Setup: Entry Point
// ================================================================
void setup() {
  wakeTime = millis(); // START TIMING

  setCpuFrequencyMhz(80); 

  rtc_gpio_deinit(AUX_PIN);
  
  Serial.begin(115200);
  while (!Serial && millis() < 2000); 

  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  
  bootCount++;

  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR Dual-Core Receiver          ║");
  Serial.println("╚════════════════════════════════════════╝");
  Serial.print("Boot Count: "); Serial.println(bootCount);

  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  pinMode(AUX_PIN, INPUT);
  
  Serial.println("Reset Reason:");
  int resetReason = rtc_get_reset_reason(0);
  print_reset_reason(resetReason);
  
  esp_reset_reason_t reset_reason = esp_reset_reason();
  
  if (reset_reason == ESP_RST_POWERON) {
    initRadio();
    enterDeepSleep();
  } 
  else if (reset_reason == ESP_RST_DEEPSLEEP) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║     WOR WAKE-UP SEQUENCE STARTING      ║");
    Serial.println("╚════════════════════════════════════════╝");
    Serial.println("Event: Wake from Deep Sleep");
    Serial.println();
    Serial.println("==============================");
    Serial.println("   WOR RECEIVE SEQUENCE START");
    Serial.println("==============================");
    
    Serial.print("WAKE: Wake-up detected at ");
    Serial.print(wakeTime);
    Serial.println(" ms");
    
    // Release GPIO holds
    gpio_hold_dis(M0_PIN);
    gpio_hold_dis(M1_PIN);
    gpio_deep_sleep_hold_dis();
    
    Serial.println();
    Serial.println("STEP 1: Re-initializing Serial & E220...");
    Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
    delay(100);
    
    e220ttl.begin();
    delay(100);
    
    radioReadyTime = millis();
    Serial.print("  Radio ready in ");
    Serial.print(radioReadyTime - wakeTime);
    Serial.println(" ms");
    Serial.println("  Module already in WOR_RECEIVER mode.");
    Serial.println();

    packetStartTime = millis();
    Serial.println("STEP 2: Launching dual-core tasks...");
    xTaskCreatePinnedToCore(commTask, "CommTask", 4096, NULL, 2, &commTaskHandle, 0);
    xTaskCreatePinnedToCore(logicTask, "LogicTask", 4096, NULL, 1, &logicTaskHandle, 1);
    Serial.println("  Tasks created on Core 0 (Comm) and Core 1 (Logic).");
    Serial.println();
  }
}

// ================================================================
// Core 0: Communication (Receive Data)
// ================================================================
void commTask(void* parameter) {
  Serial.println("STEP 3: Waiting for data packet...");
  unsigned long waitStart = millis();
  bool dataReceived = false;

  // Wait up to 12 seconds for the actual packet
  for (int i = 0; i < 120 && !dataReceived; i++) {
    if (e220ttl.available() > 0) {
      dataReceived = true;
      break;
    }
    vTaskDelay(pdMS_TO_TICKS(100));
  }

  if (dataReceived) {
    Serial.print("  Packet detected after ");
    Serial.print(millis() - waitStart);
    Serial.println(" ms");
    
    ResponseStructContainer rsc = e220ttl.receiveMessageRSSI(sizeof(Message));
    if (rsc.status.code == 1 && rsc.data != nullptr) {
      memcpy(&inbox, rsc.data, sizeof(Message));
      inbox.dateTime[39] = '\0';
      inboxReady = true;
      
      packetReceivedTime = millis();
      Serial.print("  Packet received successfully. RSSI: ");
      Serial.println(rsc.rssi);
      Serial.print("  Reception time: ");
      Serial.print(packetReceivedTime - packetStartTime);
      Serial.println(" ms");
      Serial.println();
    }
    rsc.close();
  } else {
    Serial.println("  ⚠️ Timeout - No packet received after 12 seconds.");
    Serial.println();
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

    Serial.println("STEP 4: Processing received data...");
    Serial.print("  Switch Command: ");
    Serial.println(inbox.switchData);
    Serial.print("  Timestamp: ");
    Serial.println(inbox.dateTime);
    Serial.println();    

    Serial.println("STEP 5: Executing relay toggle...");  // <-- Add this back!
    relayStartTime = millis();

    pinMode(KY002S_TRIGGER, OUTPUT);
    pinMode(KY002S_STATUS, INPUT_PULLDOWN);

    bool isCurrentlyOn = (digitalRead(KY002S_STATUS) == HIGH);
    bool requestedOn = (inbox.switchData == 1);  // 1=ON, 2=OFF

    Serial.print("  Current State: ");
    Serial.println(isCurrentlyOn ? "ON" : "OFF");
    Serial.print("  Requested State: ");
    Serial.println(requestedOn ? "ON" : "OFF");

    if (isCurrentlyOn != requestedOn) {
      // State needs to change - send toggle pulse
      digitalWrite(KY002S_TRIGGER, HIGH);
      vTaskDelay(pdMS_TO_TICKS(PULSE_MS));
      digitalWrite(KY002S_TRIGGER, LOW);

      Serial.print("  ✓ Battery Power Switched ");
      Serial.println(requestedOn ? "ON" : "OFF");
    } else {
      Serial.println("  ⚠ Already in requested state - No toggle needed");
    }
    
    relayCompleteTime = millis();
    Serial.print("  Relay operation time: ");
    Serial.print(relayCompleteTime - relayStartTime);
    Serial.println(" ms");
    Serial.println();

    // Send ACK
    Serial.println("STEP 6: Sending ACK to transmitter...");
    ackStartTime = millis();
    waitForAux();
    e220ttl.sendFixedMessage(0, TRANSMITTER_ADDRESS, CHANNEL, "ACK");
    waitForAux();
    ackCompleteTime = millis();
    
    Serial.print("  ACK sent successfully in ");
    Serial.print(ackCompleteTime - ackStartTime);
    Serial.println(" ms");
    Serial.println();

    Serial.println("==============================");
    Serial.print("   WOR RECEIVE COMPLETE (");
    Serial.print(ackCompleteTime - wakeTime);
    Serial.println(" ms)");
    Serial.println("==============================");
    Serial.println();
    
    printTimingSummary();
  }

  vTaskDelay(pdMS_TO_TICKS(500));
  enterDeepSleep();
  vTaskDelete(NULL);
}

// ================================================================
// Print Timing Summary
// ================================================================
void printTimingSummary() {
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║        TIMING BREAKDOWN                ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  Serial.println("Phase                          Duration");
  Serial.println("--------------------------------------------");
  
  if (radioReadyTime > 0) {
    Serial.print("Wake to Radio Ready:           ");
    Serial.print(radioReadyTime - wakeTime);
    Serial.println(" ms");
  }
  
  if (packetReceivedTime > 0) {
    Serial.print("Packet Reception:              ");
    Serial.print(packetReceivedTime - packetStartTime);
    Serial.println(" ms");
  }
  
  if (relayCompleteTime > 0) {
    Serial.print("Relay Toggle:                  ");
    Serial.print(relayCompleteTime - relayStartTime);
    Serial.println(" ms");
  }
  
  if (ackCompleteTime > 0) {
    Serial.print("ACK Transmission:              ");
    Serial.print(ackCompleteTime - ackStartTime);
    Serial.println(" ms");
  }
  
  Serial.println("--------------------------------------------");
  Serial.print("TOTAL CYCLE TIME:              ");
  Serial.print(ackCompleteTime - wakeTime);
  Serial.println(" ms");
  Serial.println();
}

// ================================================================
// Power Management: S3 Optimized Deep Sleep
// ================================================================
void enterDeepSleep() {
  sleepPrepTime = millis();

  if (rtc_get_reset_reason(0) == ESP_RST_POWERON) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║       ENTERING DEEP SLEEP              ║");
    Serial.println("╚════════════════════════════════════════╝");
  } 
  
  if (rtc_get_reset_reason(0) != ESP_RST_POWERON) {
    Serial.println("\n╔════════════════════════════════════════╗");
    Serial.println("║     RECEIVER CYCLE COMPLETE            ║");
    Serial.println("║       ENTERING DEEP SLEEP              ║");
    Serial.println("╚════════════════════════════════════════╝");
  }

  if(inbox.switchData == 1){
    Serial.println("\n >>> Awaiting countdown timer to expire...");
  }else if(inbox.switchData == 2){
    Serial.println("\n >>> Awaiting next web request");  
  } 

  Serial.flush();

  e220ttl.setMode(MODE_2_WOR_RECEIVER);
  waitForAux();
  vTaskDelay(pdMS_TO_TICKS(100));
  
  rtc_gpio_init(AUX_PIN);
  rtc_gpio_set_direction(AUX_PIN, RTC_GPIO_MODE_INPUT_ONLY);

  gpio_hold_en(M0_PIN);
  gpio_hold_en(M1_PIN);
  gpio_deep_sleep_hold_en();

  esp_sleep_enable_ext0_wakeup(AUX_PIN, 0);
  esp_deep_sleep_start();
}

void loop() {
  // Idle
}
