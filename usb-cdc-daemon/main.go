package main

import (
	"encoding/json"
	"fmt"
	"log"
	"os"
	"os/signal"
	"strings"
	"syscall"
	"time"

	"github.com/getlantern/systray"
	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
	"golang.org/x/sys/windows/registry"
)

const (
	VID            = "303A"
	PID            = "82E5"
	RegistryKey    = `Software\HWiNFO64\Sensors\Custom\WaKu Controller`
	ComPortTimeout = 500 * time.Millisecond // Timeout for reading from COM port
)

// TelemetryData represents the structure of the incoming JSON telemetry
type TelemetryData struct {
	ClientID string `json:"client_id"`
	Event    string `json:"event"`
	Units    string `json:"units"`
	Data     struct {
		Temperature1 float64 `json:"temperature1"`
		Temperature2 float64 `json:"temperature2"`
		FAN0         uint    `json:"FAN_0"`
		FAN1         uint    `json:"FAN_1"`
		FAN2         uint    `json:"FAN_2"`
		FAN3         uint    `json:"FAN_3"`
	} `json:"data"`
}

type sensorRegistryInfo struct {
	subKeyName  string
	displayName string
	defaultUnit string
	getValue    func(data TelemetryData) string
}

var mStatus *systray.MenuItem // Global variable for the status menu item

// getTempUnit determines the unit for temperature sensors.
func getTempUnit(td TelemetryData) string {
	if td.Units != "" {
		return td.Units
	}
	return "°C" // Default to Celsius

}

func main() {
	c := make(chan os.Signal, 1)
	signal.Notify(c, os.Interrupt, syscall.SIGTERM)
	go func() {
		<-c
		log.Println("Exiting Waku Controller Telemetry Monitor")
		processTelemetry("{\"temperature1\":0, \"temperature2\":0, \"FAN0\":0, \"FAN1\":0, \"FAN2\":0, \"FAN3\":0}")
		os.Exit(1)
	}()

	systray.Run(onReady, onExit)
}

func onReady() {
	systray.SetTitle("Monitoring Waku Controller Telemetry")
	systray.SetIcon(getTrayIcon())

	mStatus = systray.AddMenuItem("Status: Not connected", "Current connection status")
	mStatus.Disable() // Make it non-clickable

	systray.AddSeparator()
	mQuit := systray.AddMenuItem("Quit", "Quit the application")
	go func() {
		<-mQuit.ClickedCh
		processTelemetry("{\"temperature1\":0, \"temperature2\":0, \"FAN0\":0, \"FAN1\":0, \"FAN2\":0, \"FAN3\":0}")
		systray.Quit()
	}()

	go startTelemetryMonitor() // Start the telemetry monitoring in a goroutine
}

func getTrayIcon() []byte {
	iconData, err := os.ReadFile("tray.ico")
	if err != nil {
		log.Fatalf("Error reading tray icon: %v", err)
	}
	return iconData

}

func onExit() {
	// Clean up resources if necessary
	log.Println("Exiting Waku Controller Telemetry Monitor")
}

func startTelemetryMonitor() {
	var port serial.Port
	var staleRetries int = 0
	// var err error

OUTER:
	for {
		// Find the COM port
		portName, err := findDeviceComPort(VID, PID)
		if err != nil {
			log.Printf("Error finding device: %v. Retrying in 5 seconds...", err)
			systray.SetTooltip("Waku Controller: Not connected")
			mStatus.SetTitle("Status: Not connected")

			time.Sleep(5 * time.Second)
			continue
		}

		// Open the serial port
		port, err = serial.Open(portName, &serial.Mode{BaudRate: 115200}) // Assuming 115200 baud rate
		if err != nil {
			log.Printf("Error opening serial port %s: %v. Retrying in 5 seconds...", portName, err)
			systray.SetTooltip("Waku Controller: Not connected")
			mStatus.SetTitle("Status: Not connected")
			time.Sleep(5 * time.Second)
			continue
		}
		log.Printf("Successfully opened serial port %s", portName)
		systray.SetTooltip("Waku Controller: Connected")
		mStatus.SetTitle("Status: Connected")
		port.SetReadTimeout(ComPortTimeout) // Set a read timeout

		reader := make([]byte, 4096)
		var jsonBuffer []byte // Buffer to accumulate partial JSON messages

		for {
			n, err := port.Read(reader)
			if err != nil {
				if err.Error() == "The I/O operation has been aborted because of either a thread exit or an application request." {
					// This error can occur when the device is disconnected
					log.Printf("Serial port %s disconnected. Attempting to reconnect...", portName)
					systray.SetTitle("Waku Controller: Not connected")
					mStatus.SetTitle("Status: Not connected")
					port.Close()
					break // Break out of the inner loop to re-find the port
				}
				log.Printf("Error reading from serial port %s: %v", portName, err)
				// Small delay to prevent tight loop on persistent read errors
				time.Sleep(1 * time.Second)
			}

			if n == 0 {
				staleRetries++
				time.Sleep(100 * time.Millisecond) // No data, wait a bit
				if staleRetries > 10 {
					log.Printf("Retrying %d", staleRetries)
					staleRetries = 0
					systray.SetTitle("Waku Controller: Not connected")
					mStatus.SetTitle("Status: Not connected")
					port.Close()
					continue OUTER
				}
			}

			jsonBuffer = append(jsonBuffer, reader[:n]...)

			// Try to find a complete JSON object
			for {
				startIndex := -1
				endIndex := -1

				// Find the first '{'
				for i := 0; i < len(jsonBuffer); i++ {
					if jsonBuffer[i] == '{' {
						startIndex = i
						break
					}
				}

				if startIndex == -1 {
					// No start of JSON found, clear buffer or wait for more data
					jsonBuffer = []byte{} // Clear buffer if no '{' is found to avoid accumulating garbage
					break
				}

				// Find the matching '}'
				openBrackets := 0
				for i := startIndex; i < len(jsonBuffer); i++ {
					if jsonBuffer[i] == '{' {
						openBrackets++
					} else if jsonBuffer[i] == '}' {
						openBrackets--
					}
					if openBrackets == 0 && jsonBuffer[i] == '}' {
						endIndex = i
						break
					}
				}

				if endIndex != -1 {
					// Found a complete JSON object
					jsonStr := string(jsonBuffer[startIndex : endIndex+1])
					go processTelemetry(jsonStr)         // Process in a new goroutine to avoid blocking
					jsonBuffer = jsonBuffer[endIndex+1:] // Remove processed JSON from buffer
					staleRetries = 0
					// Continue to check for more JSON objects in the remaining buffer
				} else {
					// No complete JSON object yet, wait for more data
					break
				}
			}
		}
	}
}

// findDeviceComPort searches for the COM port associated with the given VID and PID.
func findDeviceComPort(vid, pid string) (string, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		log.Fatal(err)
	}
	for _, port := range ports {
		fmt.Printf("Found port: %s\n", port.Name)
		if port.IsUSB {
			fmt.Printf("   USB ID     %s:%s\n", port.VID, port.PID)
			fmt.Printf("   USB serial %s\n", port.SerialNumber)

			if strings.EqualFold(port.VID, vid) && strings.EqualFold(port.PID, pid) {
				return port.Name, nil
			}

		}
	}

	return "", fmt.Errorf("waku controller device with VID %s and PID %s not found", vid, pid)
}

// processTelemetry parses the JSON and updates the registry.
func processTelemetry(jsonStr string) {
	var telemetry TelemetryData
	err := json.Unmarshal([]byte(jsonStr), &telemetry)
	if err != nil {
		log.Printf("Error unmarshalling JSON: %v, JSON: %s", err, jsonStr)
		return
	}

	// Define sensor configurations
	sensorInfos := []sensorRegistryInfo{
		{"Temp0", "Temperature sensor 0", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temperature1) }},
		{"Temp1", "Temperature sensor 1", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temperature2) }},
		{"Fan0", "Fan Pump Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.FAN0) }},
		{"Fan1", "Fan 1 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.FAN1) }},
		{"Fan2", "Fan 2 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.FAN2) }},
		{"Fan3", "Fan 3 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.FAN3) }},
	}

	// Open/Create the main application key
	mainAppKey, _, err := registry.CreateKey(registry.CURRENT_USER, RegistryKey, registry.CREATE_SUB_KEY)
	if err != nil {
		log.Printf("Error creating/opening main registry key %s: %v", RegistryKey, err)
		return
	}

	defer mainAppKey.Close()

	for _, sensor := range sensorInfos {
		// Create/Open the specific sensor subkey
		sensorKey, _, err := registry.CreateKey(mainAppKey, sensor.subKeyName, registry.SET_VALUE)
		if err != nil {
			log.Printf("Error creating/opening sensor subkey %s\\%s: %v", RegistryKey, sensor.subKeyName, err)
			continue // Skip this sensor if its key cannot be created/opened
		}

		valueStr := sensor.getValue(telemetry)
		unitStr := sensor.defaultUnit

		// Determine unit dynamically for temperature sensors
		if sensor.subKeyName == "Temp0" || sensor.subKeyName == "Temp1" {
			unitStr = getTempUnit(telemetry)
		}

		if err := sensorKey.SetStringValue("Name", sensor.displayName); err != nil {
			log.Printf("Error setting Name for %s\\%s: %v", sensor.subKeyName, "Name", err)
		}
		if err := sensorKey.SetStringValue("Value", valueStr); err != nil {
			log.Printf("Error setting Value for %s\\%s: %v", sensor.subKeyName, "Value", err)
		}
		if err := sensorKey.SetStringValue("Unit", unitStr); err != nil {
			log.Printf("Error setting Unit for %s\\%s: %v", sensor.subKeyName, "Unit", err)
		}

		sensorKey.Close() // Close the sensor-specific key
	}

	log.Printf("Registry updated for Waku Controller. Last telemetry: %+v", telemetry.Data)
}
