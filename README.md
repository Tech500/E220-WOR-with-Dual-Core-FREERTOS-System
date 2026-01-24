# E220-WOR with Dual-Core FreeRTOS System

Project I've been working on that combines the Ebyte E220 LoRa transceiver with ESP32/ESP32-S3 microcontrollers using dual-core FreeRTOS architecture and Wake-On-Radio (WOR) functionality.

## Project Overview

This project demonstrates implementation of LoRa communication with deep sleep power management, utilizing the dual-core capabilities of ESP32 and ESP32-S3 microcontrollers. The system is designed for ultra-low-power remote applications where battery efficiency is critical.

**GitHub Repository:** [E220-WOR-with-Dual-Core-FREERTOS-System](https://github.com/Tech500/E220-WOR-with-Dual-Core-FREERTOS-System)

Video demonstration of project:  [Demonstration](https://drive.google.com/file/d/1AwHzdBJNkUyV5_DEXMmgqUM9zlnS1TRc/view?usp=sharing)

Putty log showing squence of [receiving events with event timing](https://drive.google.com/file/d/1NKLSYEWCUOPQtE2oetw9T8afeHWkJF_z/view?usp=sharing)

## Key Features

- **Dual-Core Architecture:** Leverages both cores of ESP32/ESP32-S3 with FreeRTOS task management
- **Wake-On-Radio (WOR):** E220 transceiver wakes from deep sleep on incoming messages
- **Ultra-Low Power:** Deep sleep mode with 5 mA current draw (Dev Boards)
  - *Ebyte EoRa-S3-900TB is an excellent dev board; ESP32-S3 with SX1262 LoRa Radio (µA range on battery) - future upgrade*
- **Long Range:** Up to 10 km estimated range at 30 dBm transmit power
- **Dual Platform Support:** Compatible with both ESP32 and ESP32-S3 development boards

## Technical Highlights

The system uses FreeRTOS to distribute tasks across both processor cores, with one core handling radio communications while the other manages system tasks and power states. The E220-900T30D transceiver's WOR capability allows the entire system to remain in deep sleep until a radio message arrives, making it ideal for battery-powered remote monitoring and control applications.

### The repository includes:

- Complete source code for ESP32 and ESP32-S3 variants
- E220 WOR Configurator sketch for module setup
- Technical reference documentation for the dual-core architecture
- HTML interface files for web-based interaction

## Applications

This architecture is perfect for:

- Remote sensor networks
- Battery-powered IoT devices
- Solar-powered monitoring stations
- Long-range telemetry systems
- Agricultural automation
- Environmental monitoring

## License

Released under the [MIT License](LICENSE), making it free for both personal and commercial use.

## Project Credits & Acknowledgments

This project wouldn't have been possible without the contributions and support of the following individuals and AI assistants:

**William Lucid (Tech500)** – Project author and developer

### AI Collaboration Team

This project was developed with significant assistance from various AI language models, each contributing their unique strengths to different aspects of the development:

#### Claude (Anthropic) – Primary development partner
- Advanced code architecture and FreeRTOS dual-core implementation
- Technical documentation and markdown formatting
- Code optimization and debugging assistance
- Project structure and organization
- Deep technical problem-solving for ESP32 platform specifics

#### ChatGPT (OpenAI) – Development support
- Code refinement and alternative implementation suggestions
- Documentation assistance
- General programming consultation
- Code review and improvement recommendations

#### Gemini (Google) – Research and analysis
- Technical research and background information
- Comparison analysis of different approaches
- Additional code examples and references
- Verification of technical concepts

### Special Thanks

**Renzo Mischianti (xReef)**
- LoRa E220 Library development and maintenance
- Comprehensive E220 Ebyte articles and documentation
- Community support and technical guidance
- E220 support resources and examples

**Wolfgang Ewald**
- Excellent tutorial: "Using LoRa with the EByte E220, E22 and E32 series"
- Practical hands-on guidance and real-world examples
- Valuable insights into LoRa implementation

### Component Manufacturers

- **Espressif Systems** – For the ESP32 and ESP32-S3 microcontroller platforms and FreeRTOS implementation
- **Ebyte** – For the excellent E220-900T30D LoRa transceiver modules

## Development Notes

This project represents a collaborative effort between human expertise and AI assistance, demonstrating how modern AI tools can accelerate embedded systems development while maintaining high code quality and documentation standards. Each AI assistant brought different strengths to the project, from Claude's deep technical ESP32 knowledge to Gemini's research capabilities.

The combination of human direction and AI assistance enabled rapid prototyping, comprehensive documentation, and robust implementation of complex dual-core FreeRTOS architecture.

---

I hope this project helps others working on similar LoRa and ESP32 applications! Feel free to fork, contribute, or reach out with questions.

## Questions? Suggestions? Contributions?

Please open an issue on GitHub or submit a pull request. I'm always happy to discuss the project and help others implement similar systems!

---

*Licensed under MIT License – See repository for full license details*
