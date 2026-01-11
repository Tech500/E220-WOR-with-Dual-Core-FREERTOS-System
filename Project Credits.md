/*
 * ================================================================
 * E220 WOR Remote Switch System
 * ================================================================
 * 
 * Project: Wake-On-Radio (WOR) Remote Battery Switch Control
 * Hardware: ESP32/ESP32-S3 + EBYTE E220-900T30D LoRa Module
 * 
 * Development Date: January 2026
 * 
 * CREDITS & ACKNOWLEDGMENTS:
 * ================================================================
 * 
 * Lead Developer: William Lucid (AB9NQ)
 * 
 * AI Development Team:
 *   - Claude (Anthropic) - Primary code architecture, debugging,
 *     WOR protocol implementation, dual-core optimization
 *   - Microsoft Copilot - Initial dual-core FreeRTOS structure
 *   - Google Gemini - Code refinement assistance
 * 
 * Special Thanks:
 *   - Renzo Mischianti for his E220 Library and Support Forum.
 *   - Community support from ESP32.com and RNTLabs.com
 * 
 * Technical Achievement:
 *   Successfully implemented reliable Wake-On-Radio (WOR) 
 *   communication with deep sleep power management, achieving
 *   sub-second wake-up times and stable dual-core operation.
 * 
 * Key Features:
 *   - WOR preamble + data message transmission
 *   - Deep sleep with <50µA standby current
 *   - Automatic 60-second countdown timer
 *   - Bidirectional ACK communication
 *   - Detailed timing for power analysis
 *   - Dual-core FreeRTOS architecture (no watchdog crashes)
 * 
 * Debugging Journey:
 *   This project overcame numerous challenges including:
 *   - Pin assignment corrections (M0/M1 swap)
 *   - Air data rate mismatches
 *   - RF overload at close range
 *   - Configuration persistence issues
 *   - Watchdog timer crashes
 *   - Timing synchronization between TX and RX
 * 
 * License:  MIT
 * 
 * Contact:  William Lucid 
 *           Tech500 at GitHub.com
 * 
 * Documentation & Power Analysis:
 *   Nordic PPK 2 power measurements and detailed analysis
 *   Will be available soon at Tech500's GitHub Repository.
 * 
 * ================================================================
 * "From configuration chaos to a work of art!" - William Lucid
 * ================================================================
 */
