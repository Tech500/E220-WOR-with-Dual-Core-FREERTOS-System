/*
 * E220_WOR_Configurator.ino
 * Properly configure E220 modules for WOR operation
 * 01/09/2026
 * 
 * INSTRUCTIONS:
 * 1. Upload this to ESP32
 * 2. Connect ONE E220 module at a time
 * 3. Open Serial Monitor (115200 baud)
 * 4. Send '1' to configure as TRANSMITTER (address 0x02)
 * 5. Send '2' to configure as RECEIVER (address 0x03)
 * 6. Send 'R' to read current config
 * 
 * This ensures both modules are properly configured with matching settings
 */

#include "Arduino.h"
#include "LoRa_E220.h"

// Pin definitions for ESP32
#define RXD2 16
#define TXD2 17
#define M0_PIN 21
#define M1_PIN 19
#define AUX_PIN 15

#define CHANNEL 23
#define FREQUENCY_915

LoRa_E220 e220ttl(&Serial2, AUX_PIN, M0_PIN, M1_PIN);

void waitForAux(int timeout = 5000) {
  uint32_t start = millis();
  Serial.print("  Waiting for AUX...");
  while (digitalRead(AUX_PIN) == LOW && (millis() - start < timeout)) {
    delay(10);
  }
  if (digitalRead(AUX_PIN) == HIGH) {
    Serial.println(" ✓ Ready");
  } else {
    Serial.println(" ✗ TIMEOUT!");
  }
}

void printConfiguration(Configuration config) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.println("║       CURRENT CONFIGURATION            ║");
  Serial.println("╚════════════════════════════════════════╝");
  
  Serial.print("Address High (ADDH):     0x");
  Serial.println(config.ADDH, HEX);
  Serial.print("Address Low (ADDL):      0x");
  Serial.println(config.ADDL, HEX);
  Serial.print("Channel:                 ");
  Serial.println(config.CHAN, DEC);
  
  Serial.println("\n--- Critical WOR Settings ---");
  Serial.print("Fixed Transmission:      ");
  if (config.TRANSMISSION_MODE.fixedTransmission == FT_FIXED_TRANSMISSION) {
    Serial.println("ENABLED ✓");
  } else {
    Serial.println("DISABLED ✗");
  }
  
  Serial.print("WOR Period:              ");
  Serial.println(config.TRANSMISSION_MODE.getWORPeriodByParamsDescription());
  
  Serial.print("Air Data Rate:           ");
  Serial.println(config.SPED.getAirDataRateDescription());
  
  Serial.print("UART Baud:               ");
  Serial.println(config.SPED.getUARTBaudRateDescription());
  
  Serial.print("Transmission Power:      ");
  Serial.println(config.OPTION.getTransmissionPowerDescription());
  
  Serial.println("════════════════════════════════════════\n");
}

bool configureModule(byte address, const char* roleName) {
  Serial.println("\n╔════════════════════════════════════════╗");
  Serial.print("║  CONFIGURING AS ");
  Serial.print(roleName);
  for (int i = strlen(roleName); i < 23; i++) Serial.print(" ");
  Serial.println("║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Step 1: Ensure we're in NORMAL mode
  Serial.println("Step 1: Setting MODE_0_NORMAL...");
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  
  // Step 2: Read current configuration
  Serial.println("\nStep 2: Reading current configuration...");
  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code != 1) {
    Serial.println("✗ Failed to read configuration!");
    c.close();
    return false;
  }
  
  Configuration config = *(Configuration*) c.data;
  c.close();
  
  Serial.println("Current settings:");
  printConfiguration(config);
  
  // Step 3: Modify configuration
  Serial.println("Step 3: Modifying configuration...");
  
  config.ADDH = 0x00;
  config.ADDL = address;
  config.CHAN = CHANNEL;
  
  // UART settings
  config.SPED.uartBaudRate = UART_BPS_9600;
  config.SPED.airDataRate = AIR_DATA_RATE_010_24;  // 2.4kbps
  config.SPED.uartParity = MODE_00_8N1;
  
  // Transmission mode - CRITICAL for WOR
  config.TRANSMISSION_MODE.fixedTransmission = FT_FIXED_TRANSMISSION;  // MUST be enabled
  config.TRANSMISSION_MODE.WORPeriod = WOR_2000_011;  // 2000ms
  config.TRANSMISSION_MODE.enableLBT = LBT_DISABLED;
  config.TRANSMISSION_MODE.enableRSSI = RSSI_DISABLED;
  
  // Power settings
  config.OPTION.transmissionPower = POWER_22;  // 22dBm
  config.OPTION.subPacketSetting = SPS_200_00;
  config.OPTION.RSSIAmbientNoise = RSSI_AMBIENT_NOISE_DISABLED;
  
  // Step 4: Write configuration with PERMANENT save
  Serial.println("\nStep 4: Writing configuration to module...");
  Serial.println("  (This will save to permanent memory)");
  
  ResponseStatus rs = e220ttl.setConfiguration(config, WRITE_CFG_PWR_DWN_SAVE);
  
  Serial.print("  Write status: ");
  Serial.println(rs.getResponseDescription());
  
  if (rs.code != 1) {
    Serial.println("✗ Configuration write FAILED!");
    return false;
  }
  
  Serial.println("✓ Configuration written successfully");
  delay(200);
  
  // Step 5: Set back to normal mode and wait
  Serial.println("\nStep 5: Returning to NORMAL mode...");
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  
  // Step 6: Verify configuration was saved
  Serial.println("\nStep 6: Verifying configuration...");
  
  c = e220ttl.getConfiguration();
  if (c.status.code != 1) {
    Serial.println("✗ Failed to verify configuration!");
    c.close();
    return false;
  }
  
  Configuration verify = *(Configuration*) c.data;
  c.close();
  
  printConfiguration(verify);
  
  // Check critical settings
  bool success = true;
  
  if (verify.ADDL != address) {
    Serial.print("✗ Address verification FAILED! Got 0x");
    Serial.print(verify.ADDL, HEX);
    Serial.print(", expected 0x");
    Serial.println(address, HEX);
    success = false;
  } else {
    Serial.print("✓ Address verified: 0x");
    Serial.println(verify.ADDL, HEX);
  }
  
  if (verify.CHAN != CHANNEL) {
    Serial.print("✗ Channel verification FAILED! Got ");
    Serial.print(verify.CHAN);
    Serial.print(", expected ");
    Serial.println(CHANNEL);
    success = false;
  } else {
    Serial.print("✓ Channel verified: ");
    Serial.println(verify.CHAN);
  }
  
  if (verify.TRANSMISSION_MODE.fixedTransmission != FT_FIXED_TRANSMISSION) {
    Serial.println("✗ Fixed Transmission NOT enabled!");
    success = false;
  } else {
    Serial.println("✓ Fixed Transmission verified: ENABLED");
  }
  
  if (verify.TRANSMISSION_MODE.WORPeriod != WOR_2000_011) {
    Serial.println("✗ WOR Period mismatch!");
    success = false;
  } else {
    Serial.println("✓ WOR Period verified: 2000ms");
  }
  
  Serial.println();
  
  if (success) {
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║    ✓ CONFIGURATION SUCCESSFUL!         ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    return true;
  } else {
    Serial.println("╔════════════════════════════════════════╗");
    Serial.println("║    ✗ CONFIGURATION FAILED!             ║");
    Serial.println("╚════════════════════════════════════════╝\n");
    return false;
  }
}

void readCurrentConfig() {
  Serial.println("\nReading current configuration...");
  
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  
  ResponseStructContainer c = e220ttl.getConfiguration();
  if (c.status.code == 1) {
    Configuration config = *(Configuration*) c.data;
    printConfiguration(config);
  } else {
    Serial.println("✗ Failed to read configuration!");
  }
  c.close();
}

void setup() {
  Serial.begin(115200);
  delay(1000);
  
  Serial.println("\n\n");
  Serial.println("╔════════════════════════════════════════╗");
  Serial.println("║   E220 WOR CONFIGURATION TOOL          ║");
  Serial.println("║   Version 1.0                          ║");
  Serial.println("╚════════════════════════════════════════╝\n");
  
  // Initialize pins
  pinMode(AUX_PIN, INPUT);
  pinMode(M0_PIN, OUTPUT);
  pinMode(M1_PIN, OUTPUT);
  
  Serial.println("Initializing UART (9600 baud)...");
  Serial2.begin(9600, SERIAL_8N1, RXD2, TXD2);
  delay(100);
  
  Serial.println("Initializing E220 module...");
  e220ttl.begin();
  delay(100);
  
  e220ttl.setMode(MODE_0_NORMAL);
  delay(100);
  waitForAux();
  
  Serial.println("\n✓ Initialization complete!\n");
  
  Serial.println("════════════════════════════════════════");
  Serial.println("COMMANDS:");
  Serial.println("════════════════════════════════════════");
  Serial.println("1 - Configure as TRANSMITTER (Addr 0x02)");
  Serial.println("2 - Configure as RECEIVER (Addr 0x03)");
  Serial.println("R - Read current configuration");
  Serial.println("N - Set Normal Mode");
  Serial.println("W - Set WOR Transmitter Mode");
  Serial.println("X - Set WOR Receiver Mode");
  Serial.println("════════════════════════════════════════\n");
  
  Serial.println("INSTRUCTIONS:");
  Serial.println("1. Connect ONE module at a time");
  Serial.println("2. Send '1' to configure first module as TX");
  Serial.println("3. Disconnect and connect second module");
  Serial.println("4. Send '2' to configure second module as RX");
  Serial.println("5. Both modules will have matching settings");
  Serial.println("   except for address (0x02 vs 0x03)\n");
}

void loop() {
  if (Serial.available() > 0) {
    char cmd = Serial.read();
    
    // Clear any extra characters
    while (Serial.available()) Serial.read();
    
    switch (cmd) {
      case '1':
        configureModule(0x02, "TRANSMITTER");
        Serial.println("✓ Module configured as TRANSMITTER");
        Serial.println("  Address: 0x02");
        Serial.println("  Role: Sends WOR messages to 0x03\n");
        break;
        
      case '2':
        configureModule(0x03, "RECEIVER");
        Serial.println("✓ Module configured as RECEIVER");
        Serial.println("  Address: 0x03");
        Serial.println("  Role: Receives WOR messages from 0x02\n");
        break;
        
      case 'R':
      case 'r':
        readCurrentConfig();
        break;
        
      case 'N':
      case 'n':
        Serial.println("Setting MODE_0_NORMAL...");
        e220ttl.setMode(MODE_0_NORMAL);
        delay(100);
        waitForAux();
        Serial.println("✓ Normal mode set\n");
        break;
        
      case 'W':
      case 'w':
        Serial.println("Setting MODE_1_WOR_TRANSMITTER...");
        e220ttl.setMode(MODE_1_WOR_TRANSMITTER);
        delay(100);
        waitForAux();
        Serial.println("✓ WOR Transmitter mode set\n");
        break;
        
      case 'X':
      case 'x':
        Serial.println("Setting MODE_2_WOR_RECEIVER...");
        e220ttl.setMode(MODE_2_WOR_RECEIVER);
        delay(100);
        waitForAux();
        Serial.println("✓ WOR Receiver mode set");
        Serial.println("  Module is now sleeping, waiting for WOR wake\n");
        break;
        
      default:
        // Ignore other characters
        break;
    }
  }
  
  delay(10);
}