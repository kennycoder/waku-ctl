# WaKu Controller Board Manual

The **WaKu Controller** is a comprehensive hardware solution for managing custom water-cooling loops and high-performance air-cooled systems. It integrates fan control, pump management, temperature sensing, and ARGB lighting into a single board, with safety features to protect your hardware.

## Hardware Overview

### Power & Connectivity
*   **Power Input**: Powered via a standard **SATA power cable** from your PSU.
*   **Programming/Debug Port**: The **Top USB port** (near the SATA connector) is used for flashing firmware and serial monitoring. It utilizes an FTDI FT231X chip.
*   **Data Port**: The **Side USB port** connects to your PC for software integration (Config Daemon, HWInfo64 data).

### Fan & Pump Headers (Left Side)
There are **4x 4-pin PWM headers** available for fans and pumps.
*   **Pump Header**: The first header is dedicated to the pump (marked with a pump icon on the casing), though it can be used for fans if desired.
*   **Capacity**: Each PWM header can support **3-4 fans** via splitters, depending on their power consumption.
*   **Power Control**: A **2-pin Female Header** is located on this side.
    *   Connect this to your motherboard's Power Switch pins using the provided splitter cable.
    *   This allows the WaKu Controller to physically turn off the PC via optocoupler if a critical alarm triggers.

### Sensors & Lighting (Right Side)
*   **Temperature Sensors**: **3x 2-pin headers** for standard G1/4" 10k thermistors.
    *   *Note: The controller also calculates 3 "Virtual Sensors" representing the delta (difference) between physical sensors.*
*   **ARGB Headers**: **2x 3-pin 5V ARGB headers** for connecting LED strips or components.
    *   Supports up to 96 LEDs per strip (configurable).

### Motherboard Integration (Bottom)
*   **RPM Signal Proxy**: Connects to your motherboard's **CPU_FAN** header.
    *   This sends a simulated RPM signal to your motherboard so it detects a CPU fan is present and running.
    *   You can configure which of the 4 controller fans is proxied to this header.
*   **External ARGB Input**: Connects to an external ARGB controller or motherboard ARGB header.
    *   Allows "Passthrough Mode", letting your motherboard control the LEDs connected to the WaKu Controller.

### Wireless
*   **WiFi**: Integrated ESP32-S3 WiFi.
*   **Antenna**: An IPX connector is available on the board if the signal strength is low inside the case.

---

## Installation & Setup

### 1. Physical Installation
1.  Mount the board in your case.
2.  Connect the **SATA power cable**.
3.  Connect fans and pumps to the PWM headers.
4.  Connect thermistors to the temperature headers.
5.  (Optional) Connect the Power Switch splitter to your motherboard if you want emergency shutdown protection.
6.  (Optional) Connect the Bottom RPM header to your motherboard's CPU_FAN header to avoid BIOS errors.

### 2. Initial Configuration
When first powered on, the controller will create a WiFi Access Point.

1.  On your phone or PC, search for the WiFi network named **`waku-ctl`**.
2.  Connect to it (no password by default).
3.  Open a web browser and navigate to **`http://192.168.4.1`**.
4.  Follow the on-screen instructions to configure your home WiFi credentials and basic settings.
5.  Check the **OLED Screen** on the device for status information (IP address, current mode).

---

## Features & Configuration

### Fan Control
Access the web interface (at the IP shown on the OLED) to configure fan curves.
*   **Modes**:
    *   **Curve**: Set a custom Temperature vs. Duty Cycle (%) curve.
    *   **PID**: Uses a PID algorithm to maintain a specific target temperature.
*   **Sources**: Fans can react to any of the 3 physical temperature sensors or the 3 virtual delta sensors.

### RGB Lighting
*   **Effects**: Static, Gradient Wave, Moving Gradient, Rainbow.
*   **Passthrough**: Select "Passthrough" mode to hand over control to the External ARGB Input header (e.g., for syncing with motherboard software like Aura Sync, Mystic Light).

### Safety & Alarms
The controller monitors system health and can react to failures.
*   **Triggers**:
    *   **High Temp**: Temperature exceeds a defined threshold.
    *   **Low RPM**: Fan/Pump speed drops below a defined threshold.
*   **Actions**:
    *   **Visual/Audio**: The OLED displays the alarm and the onboard buzzer sounds.
    *   **Emergency Shutdown**: If configured, the controller triggers the Power Switch header to immediately shut down the PC to prevent hardware damage.

### OLED Display
The onboard screen cycles through information screens. You can manually cycle screens by pressing the **Reset Button** briefly.
1.  **Overview**: IP, Status, Alarm State.
2.  **Temperatures**: Readings from T1, T2, T3.
3.  **Fan Speeds**: RPM readings for all 4 headers.
4.  **RGB Status**: Current mode for LED strips.

### Reset Button Functions
*   **Short Press**: Cycle OLED screen page.
*   **Long Press (> 5 seconds)**: Factory Reset (clears WiFi and configuration).

---

## Software Integration
*   **Web Interface**: Full control via any browser on the local network.
*   **MQTT**: Supports publishing telemetry to an MQTT broker for integration with Home Assistant or other dashboards.
*   **USB Data**: Connect the Side USB port to use the desktop companion app (Daemon) for integrating sensor data from **HWInfo64** (e.g., controlling fans based on GPU or CPU package temp).