# waku-ctl

![waku-ctl banner](https://raw.githubusercontent.com/kennycoder/waku-ctl/refs/heads/main/media/header.png)

waku-ctl is an open-source water cooling controller designed to give you ultimate command over your custom water loop, freeing you from the limitations of BIOS profiles and proprietary software.

This project contains the hardware and software for the Waku Control device.

## Project Structure

*   `/pcb`: Contains the PCB design files from Altium.
*   `/case`: Contains the STL files for the 3D printable case.
*   `/src`: Contains the source code for the ESP32 firmware.
*   `/data`: Contains the web interface files.
*   `/usb-cdc-daemon`: Contains a small system tray daemon that reads data via USB and submits it to HWiNFO64.

The firmware is developed using Visual Studio Code with the PlatformIO extension.

## Building the Firmware

1.  Install [Visual Studio Code](https://code.visualstudio.com/).
2.  Install the [PlatformIO IDE extension](https://platformio.org/platformio-ide) from the Visual Studio Code marketplace.
3.  Clone this repository.
4.  Open the cloned repository folder in Visual Studio Code.
5.  PlatformIO will automatically detect the `platformio.ini` file and install the necessary dependencies.
6.  Click the "Build" button in the PlatformIO toolbar (the checkmark icon) to compile the firmware.
7.  Click the "Upload" button (the right arrow icon) to flash the firmware to your connected ESP32-S3 board.
8.  Click "Build Filesystem Image" and "Upload Filesystem Image"
9.  Reset the device and connect to its access point under the name of "waku-ctl" to finish setup.

## Building the USB CDC tray daemon for HWInfo64 integration

1.  Install [golang](https://go.dev/doc/install)
2.  Enter `usb-cdc-daemon` folder and run `go build .`
3.  Execute the binary and check HWInfo64, you should start getting telemetry

## Hardware

The hardware is based on an ESP32-S3 N16R8 development board or a board with a similar pinout.

### Main components

| Designator                                    | Quantity | Manufacturer                | Part Number   |
| --------------------------------------------- | -------- | --------------------------- | -------------------------- |
| Analog to Digital Converter                   | 1        | Texas Instruments           | ADS1115IDGSR               |
| Multiplexer                                   | 1        | Texas Instruments           | CD4053BPWR                 |
| 100nF Capacitors                              | 7        | Samsung Electro-Mechanics   | CL31B104KBCNNNC            |
| 8 channel Voltage Level Shiter                | 1        | Texas Instruments           | TXS0108EPWR                |
| 2 channel Voltage Level Shiter                | 1        | Texas Instruments           | TXS0102DCTT                |
| 10k Resistors                                 | 8        | UNI-ROYAL(Uniroyal Elec)    | 1206W4F1002T5E             |
| Schmitt Trigger                               | 1        | HTC Korea TAEJIN Tech       | 74HC14D                    |
| Reset Button                                  | 1        | Omron                       | B3FS-1000P                 |
| Passive Speaker/Buzzer                        | 1        | XHXDZ                       | HC9042-16                  |

### PCB layout

![waku-ctl banner](https://raw.githubusercontent.com/kennycoder/waku-ctl/refs/heads/main/media/pcbs.png)
