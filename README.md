# TSR PIR Bell

**An Open-Source Split PIR Doorbell based on ChirpIoT (Chinese LoRa) + ESP32S3 + CW32L010**

[![OSHW](https://img.shields.io/badge/Open--Source-Hardware-blue)](https://oshwhub.com/motozilog/project_ryfkuctv)
[![License: CC BY-NC-SA 4.0](https://img.shields.io/badge/License-CC%20BY--NC--SA%204.0-lightgrey.svg)](https://creativecommons.org/licenses/by-nc-sa/4.0/)

## 📖 Introduction

Traditional split wireless doorbells usually rely on low-cost superheterodyne RF modules. While cost-effective, these modules typically only support one-way transmission—meaning the indoor unit cannot actively query the status of the outdoor unit.

To solve this, and noting the lack of ChirpIoT-based applications in the open-source community, **TSR PIR Bell** utilizes the domestic "Chinese LoRa" — **ChirpIoT** technology for communication. This enables full-duplex data exchange and allows the indoor unit to actively monitor the outdoor unit's status. To our knowledge, this may be the **first open-source project on OSHWHub utilizing ChirpIoT**, serving as a reference for future applications.

## ✨ Key Features

*   **Full-Duplex Communication:** Utilizes ChirpIoT (E29-400T22S module) for reliable, long-range, and bi-directional communication between the indoor and outdoor units.
*   **Customizable Audio:** Unlike traditional OTP-chip doorbells, this project uses an **ESP32S3** with a built-in Web UI, allowing users to easily upload and customize their own welcome/chime MP3 files.
*   **Cost-Effective Storage:** Since typical greeting audio files (e.g., "Hello, welcome") are under 5 seconds, even 10 different chimes take less than 60 seconds in total. The ESP32S3's onboard SPI Flash (partitioned with LittleFS) provides ample space for audio storage, completely eliminating the need for expensive external storage (like TF cards or SD NAND).
*   **Multi-Mode Configuration:** The Web UI allows users to independently configure the trigger modes (Welcome / Doorbell / Alarm) and corresponding audio tracks for each outdoor unit (up to 4 units).
*   **Low Power & Security:** The outdoor unit uses a **CW32L010** (Cortex-M0+) for ultra-low power consumption, supporting both PIR motion detection and manual key triggers. It is powered by a lithium battery and uses a specialized 2.5*0.7mm DC jack for charging to prevent damage from high-voltage attacks.

## 🛠️ Hardware Architecture

*   **Indoor Unit (Receiver):**
    *   **MCU:** ESP32S3 (with 16MB Flash & 8MB PSRAM)
    *   **Audio:** ES8311 Codec + NS4150B PA (Power Amplifier)
    *   **Wireless:** E29-400T22S (ChirpIoT)
    *   **Power:** Powered via Type-C or 86-box 220V-to-5V module. *(Note: For 3D-printed enclosures, Type-C power is highly recommended for long-term use due to the material's lack of flame retardancy and heat resistance).*
*   **Outdoor Unit (Transmitter):**
    *   **MCU:** CW32L010 (ARM Cortex-M0+)
    *   **Sensors:** PIR (Human infrared) + Physical Push Button
    *   **Wireless:** E29-400T22S (ChirpIoT)
    *   **Power:** Lithium battery (Charged via 2.5*0.7mm DC jack)

## 💻 Development Environments

*   **ESP32S3 (Indoor Unit) Firmware:**
    *   OS: Windows 10
    *   IDE: VS Code `1.137.0` 
    *   Extension: `espressif.esp-idf-extension` `2.3.0`
    *   Framework: ESP-IDF `v5.5.5`
*   **CW32L010 (Outdoor Unit) Firmware:**
    *   IDE: uVision `V5.38.0.0` Community Edition (Keil)
    *   Compiler: ARM Compiler `V5.06 update 7 (build 960)`

## 💻 Software & Tools

*   **Web Interface:** Provides a user-friendly GUI for configuring WiFi, volume, LED brightness, audio uploads, and viewing real-time battery levels and online status of outdoor units.
*   **PC Audio Tool:** The ESP-IDF MP3 decoder library has limited format support. Therefore, a PC-side tool (WAV/MP3 to MP3 converter) was developed to ensure uploaded audio files are strictly compatible. (Since this is only needed occasionally, no web-based tool was developed).
*   **Debugging "Black Magic":** The Web UI includes a hidden developer console. Press `F12` and type `AT('HEX_DATA')` to directly inject simulated ChirpIoT packets into the system for testing without actual hardware.

## 🔗 Links & Resources

*   **Hardware Design & Schematic:** [OSHWHub Project Link](https://oshwhub.com/motozilog/project_ryfkuctv)
*   **Architecture & Protocol:** Please refer to the source code and attached documentation for detailed protocol definitions.

## 📜 License

This project is licensed under the **Creative Commons Attribution-NonCommercial-ShareAlike 4.0 International (CC BY-NC-SA 4.0)** license. 

You are free to share and adapt the material, provided you give appropriate credit, do not use the material for commercial purposes, and distribute your contributions under the same license.

For more details, please see the [LICENSE](LICENSE) file or visit: https://creativecommons.org/licenses/by-nc-sa/4.0/

---
*Note: Some codes in this project were developed with the assistance of AI technology (DeepSeek), and this README was translated by AI technology.*