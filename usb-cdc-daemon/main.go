package main

import (
	"encoding/json"
	"fmt"
	"image/color"
	"log"
	"strings"
	"sync"
	"time"

	"fyne.io/fyne/v2"
	"fyne.io/fyne/v2/app"
	"fyne.io/fyne/v2/canvas"
	"fyne.io/fyne/v2/container"
	"fyne.io/fyne/v2/dialog"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/widget"
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
		Temps struct {
			Temperature1 float64 `json:"TEMP_1"`
			Temperature2 float64 `json:"TEMP_2"`
			Temperature3 float64 `json:"TEMP_3"`
		} `json:"temps"`
		Fans struct {
			FAN0 uint `json:"FAN_PUMP"`
			FAN1 uint `json:"FAN_1"`
			FAN2 uint `json:"FAN_2"`
			FAN3 uint `json:"FAN_3"`
		} `json:"fans"`
	} `json:"data"`
}

// Settings represents the structure of the device settings
type Settings struct {
	SSID           string `json:"ssid"`
	Password       string `json:"password"`
	Hostname       string `json:"hostname"`
	TelemetryItv   int    `json:"tel_itv"`
	SetupDone      bool   `json:"setup_done"`
	OfflineMode    bool   `json:"offline_mode"`
	Units          string `json:"units"`
	MqttBroker     string `json:"mqtt_broker"`
	MqttTopic      string `json:"mqtt_topic"`
	MqttEnable     bool   `json:"mqtt_enable"`
	MqttUsername   string `json:"mqtt_username"`
	MqttPassword   string `json:"mqtt_password"`
	MqttPort       int    `json:"mqtt_port"`
	FanPassthrough int    `json:"fan_passthrough"`
}

// LedSettings represents the configuration for an RGB Strip
type LedSettings struct {
	Mode       int    `json:"mode"`
	Speed      int    `json:"speed"`
	StartColor uint32 `json:"start_color"`
	EndColor   uint32 `json:"end_color"`
	NumLeds    int    `json:"num_leds"`
}

type sensorRegistryInfo struct {
	subKeyName  string
	displayName string
	defaultUnit string
	getValue    func(data TelemetryData) string
}

type SensorDisplay struct {
	Name  string
	Value string
	Unit  string
}

var (
	dataMutex  sync.RWMutex
	sensorData []SensorDisplay
	dataList   *widget.Table

	// Serial Port
	globalPort serial.Port
	portMutex  sync.Mutex

	// Settings
	currentSettings Settings
	settingsMutex   sync.Mutex

	// RGB Settings
	rgbSettings map[string]LedSettings
	rgbMutex    sync.Mutex

	// UI Elements for Settings
	ssidEntry         *widget.Entry
	passwordEntry     *widget.Entry
	hostnameEntry     *widget.Entry
	unitsSelect       *widget.Select
	offlineCheck      *widget.Check
	mqttEnableCheck   *widget.Check
	mqttBrokerEntry   *widget.Entry
	mqttTopicEntry    *widget.Entry
	mqttUserEntry     *widget.Entry
	mqttPassEntry     *widget.Entry
	mqttPortEntry     *widget.Entry
	telItvSelect      *widget.Select
	fanPassthroughSel *widget.Select

	// UI Elements for RGB
	// Pointers to widgets to update them
	rgbWidgets map[string]struct {
		ModeSelect     *widget.Select
		SpeedSlider    *widget.Slider
		NumLedsSlider  *widget.Slider
		StartColorRect *canvas.Rectangle
		EndColorRect   *canvas.Rectangle
		StartColorBtn  *widget.Button
		EndColorBtn    *widget.Button
		SpeedLabel     *widget.Label
		NumLedsLabel   *widget.Label
	}

	mainWindow fyne.Window

	// Define sensor configurations
	sensorInfos = []sensorRegistryInfo{
		{"Temp0", "Temperature sensor 0", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature1) }},
		{"Temp1", "Temperature sensor 1", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature2) }},
		{"Temp2", "Temperature sensor 2", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature3) }},
		{"Fan0", "Fan Pump Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.Fans.FAN0) }},
		{"Fan1", "Fan 1 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.Fans.FAN1) }},
		{"Fan2", "Fan 2 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.Fans.FAN2) }},
		{"Fan3", "Fan 3 Speed", "RPM", func(td TelemetryData) string { return fmt.Sprintf("%d", td.Data.Fans.FAN3) }},
	}
)

func getTempUnit(td TelemetryData) string {
	if td.Units != "" {
		return td.Units
	}
	return "°C" // Default to Celsius
}

func main() {
	a := app.New()
	w := a.NewWindow("Waku Controller Telemetry")
	mainWindow = w

	// Initialize sensorData with empty values based on sensorInfos
	for _, info := range sensorInfos {
		sensorData = append(sensorData, SensorDisplay{
			Name:  info.displayName,
			Value: "-",
			Unit:  info.defaultUnit,
		})
	}

	// Create Tabs
	homeTab := container.NewTabItem("Home", makeHomeTab())
	curvesTab := container.NewTabItem("Curves", makeCurvesTab())
	rgbTab := container.NewTabItem("RGB", makeRgbTab())
	settingsTab := container.NewTabItem("Settings", makeSettingsTab())

	tabs := container.NewAppTabs(homeTab, curvesTab, rgbTab, settingsTab)
	tabs.SetTabLocation(container.TabLocationLeading)

	w.SetContent(tabs)
	w.Resize(fyne.NewSize(800, 600)) // Increased size for tabs

	go startTelemetryMonitor()

	w.ShowAndRun()
}

func makeHomeTab() fyne.CanvasObject {
	dataList = widget.NewTable(
		func() (int, int) {
			dataMutex.RLock()
			defer dataMutex.RUnlock()
			return len(sensorData), 3
		},
		func() fyne.CanvasObject {
			return widget.NewLabel("Cell placeholder")
		},
		func(i widget.TableCellID, o fyne.CanvasObject) {
			dataMutex.RLock()
			defer dataMutex.RUnlock()
			if i.Row >= len(sensorData) {
				return
			}
			item := sensorData[i.Row]
			label := o.(*widget.Label)

			switch i.Col {
			case 0:
				label.SetText(item.Name)
			case 1:
				label.SetText(item.Value)
			case 2:
				label.SetText(item.Unit)
			}
		},
	)

	dataList.SetColumnWidth(0, 200)
	dataList.SetColumnWidth(1, 100)
	dataList.SetColumnWidth(2, 50)

	return container.NewBorder(nil, nil, nil, nil, dataList)
}

func makeCurvesTab() fyne.CanvasObject {
	return widget.NewLabel("Curves - Placeholder")
}

func makeRgbTab() fyne.CanvasObject {
	rgbWidgets = make(map[string]struct {
		ModeSelect     *widget.Select
		SpeedSlider    *widget.Slider
		NumLedsSlider  *widget.Slider
		StartColorRect *canvas.Rectangle
		EndColorRect   *canvas.Rectangle
		StartColorBtn  *widget.Button
		EndColorBtn    *widget.Button
		SpeedLabel     *widget.Label
		NumLedsLabel   *widget.Label
	})

	// Initialize default map
	rgbSettings = make(map[string]LedSettings)
	rgbSettings["LED_0"] = LedSettings{Mode: 0, Speed: 10, NumLeds: 10, StartColor: 0xFF0000, EndColor: 0x00FF00}
	rgbSettings["LED_1"] = LedSettings{Mode: 0, Speed: 10, NumLeds: 10, StartColor: 0x0000FF, EndColor: 0xFFFF00}

	led0 := makeRgbSection("LED_0", "ARGB Header #1")
	led1 := makeRgbSection("LED_1", "ARGB Header #2")

	refreshBtn := widget.NewButton("Refresh", func() {
		sendCommand("get-rgb")
	})

	saveBtn := widget.NewButton("Save Colors", func() {
		saveRgb()
	})

	return container.NewBorder(nil, container.NewHBox(refreshBtn, saveBtn), nil, nil, container.NewVScroll(container.NewVBox(led0, widget.NewSeparator(), led1)))
}

func makeSettingsTab() fyne.CanvasObject {
	ssidEntry = widget.NewEntry()
	passwordEntry = widget.NewPasswordEntry()
	hostnameEntry = widget.NewEntry()

	unitsSelect = widget.NewSelect([]string{"C", "F"}, nil)
	unitsSelect.SetSelected("C")

	offlineCheck = widget.NewCheck("Offline Mode (AP Only)", nil)

	fanPassthroughSel = widget.NewSelect([]string{"FAN_PUMP", "FAN_1", "FAN_2", "FAN_3", "None"}, nil)

	mqttEnableCheck = widget.NewCheck("Enable MQTT", func(checked bool) {
		if checked {
			mqttBrokerEntry.Enable()
			mqttTopicEntry.Enable()
			mqttUserEntry.Enable()
			mqttPassEntry.Enable()
			mqttPortEntry.Enable()
			telItvSelect.Enable()
		} else {
			mqttBrokerEntry.Disable()
			mqttTopicEntry.Disable()
			mqttUserEntry.Disable()
			mqttPassEntry.Disable()
			mqttPortEntry.Disable()
			telItvSelect.Disable()
		}
	})

	mqttBrokerEntry = widget.NewEntry()
	mqttTopicEntry = widget.NewEntry()
	mqttUserEntry = widget.NewEntry()
	mqttPassEntry = widget.NewPasswordEntry()
	mqttPortEntry = widget.NewEntry() // TODO: Validate as int

	telItvSelect = widget.NewSelect([]string{"5000", "10000", "30000", "60000"}, nil)

	form := widget.NewForm(
		widget.NewFormItem("WiFi Network", ssidEntry),
		widget.NewFormItem("WiFi Password", passwordEntry),
		widget.NewFormItem("Hostname", hostnameEntry),
		widget.NewFormItem("Units", unitsSelect),
		widget.NewFormItem("", offlineCheck),
		widget.NewFormItem("Fan Passthrough", fanPassthroughSel),
		widget.NewFormItem("MQTT Setup", layout.NewSpacer()), // Divider equivalent
		widget.NewFormItem("", mqttEnableCheck),
		widget.NewFormItem("Broker", mqttBrokerEntry),
		widget.NewFormItem("Topic", mqttTopicEntry),
		widget.NewFormItem("Username", mqttUserEntry),
		widget.NewFormItem("Password", mqttPassEntry),
		widget.NewFormItem("Port", mqttPortEntry),
		widget.NewFormItem("Telemetry Interval (ms)", telItvSelect),
	)

	saveBtn := widget.NewButton("Save Settings", func() {
		// Collect data and send
		saveSettings()
	})

	refreshBtn := widget.NewButton("Refresh", func() {
		sendCommand("get-settings")
	})

	return container.NewBorder(nil, container.NewHBox(refreshBtn, saveBtn), nil, nil, container.NewVScroll(form))
}

func saveSettings() {
	s := Settings{}
	s.SSID = ssidEntry.Text
	s.Password = passwordEntry.Text
	s.Hostname = hostnameEntry.Text
	s.Units = unitsSelect.Selected
	s.OfflineMode = offlineCheck.Checked
	// Fan Passthrough mapping
	switch fanPassthroughSel.Selected {
	case "FAN_PUMP":
		s.FanPassthrough = 0
	case "FAN_1":
		s.FanPassthrough = 1
	case "FAN_2":
		s.FanPassthrough = 2
	case "FAN_3":
		s.FanPassthrough = 3
	default:
		s.FanPassthrough = -1
	}

	s.MqttEnable = mqttEnableCheck.Checked
	s.MqttBroker = mqttBrokerEntry.Text
	s.MqttTopic = mqttTopicEntry.Text
	s.MqttUsername = mqttUserEntry.Text
	s.MqttPassword = mqttPassEntry.Text
	// Parse Ints
	// ignoring errors for simplicity in this step, should handle better
	fmt.Sscanf(mqttPortEntry.Text, "%d", &s.MqttPort)
	fmt.Sscanf(telItvSelect.Selected, "%d", &s.TelemetryItv)

	payload, err := json.Marshal(s)
	if err != nil {
		dialog.ShowError(err, mainWindow)
		return
	}

	sendCommand("save-settings " + string(payload))
}

func makeRgbSection(id string, title string) fyne.CanvasObject {
	label := widget.NewLabelWithStyle(title, fyne.TextAlignLeading, fyne.TextStyle{Bold: true})

	modeSelect := widget.NewSelect([]string{"Off", "Static single color", "2 color wave effect", "2 color moving effect", "Rainbow effect", "Pass-through"}, nil)
	modeSelect.SetSelectedIndex(0)

	numLedsLabel := widget.NewLabel("10")
	numLedsSlider := widget.NewSlider(1, 96)
	numLedsSlider.Value = 10
	numLedsSlider.OnChanged = func(f float64) {
		numLedsLabel.SetText(fmt.Sprintf("%d", int(f)))
	}

	speedLabel := widget.NewLabel("10")
	speedSlider := widget.NewSlider(1, 100)
	speedSlider.Value = 10
	speedSlider.OnChanged = func(f float64) {
		speedLabel.SetText(fmt.Sprintf("%d", int(f)))
	}

	startColorRect := canvas.NewRectangle(color.RGBA{255, 0, 0, 255})
	startColorRect.SetMinSize(fyne.NewSize(30, 30))

	// Helper to update rect color
	updateRect := func(rect *canvas.Rectangle, c color.Color) {
		rect.FillColor = c
		rect.Refresh()
	}

	startColorBtn := widget.NewButton("Pick Color 1", func() {
		dialog.ShowColorPicker("Pick Start Color", "Pick the starting color for the effect", func(c color.Color) {
			updateRect(startColorRect, c)
		}, mainWindow)
	})

	endColorRect := canvas.NewRectangle(color.RGBA{0, 255, 0, 255})
	endColorRect.SetMinSize(fyne.NewSize(30, 30))

	endColorBtn := widget.NewButton("Pick Color 2", func() {
		dialog.ShowColorPicker("Pick End Color", "Pick the ending color for the effect", func(c color.Color) {
			updateRect(endColorRect, c)
		}, mainWindow)
	})

	// Store widgets
	rgbWidgets[id] = struct {
		ModeSelect     *widget.Select
		SpeedSlider    *widget.Slider
		NumLedsSlider  *widget.Slider
		StartColorRect *canvas.Rectangle
		EndColorRect   *canvas.Rectangle
		StartColorBtn  *widget.Button
		EndColorBtn    *widget.Button
		SpeedLabel     *widget.Label
		NumLedsLabel   *widget.Label
	}{
		ModeSelect:     modeSelect,
		SpeedSlider:    speedSlider,
		NumLedsSlider:  numLedsSlider,
		StartColorRect: startColorRect,
		EndColorRect:   endColorRect,
		StartColorBtn:  startColorBtn,
		EndColorBtn:    endColorBtn,
		SpeedLabel:     speedLabel,
		NumLedsLabel:   numLedsLabel,
	}

	return container.NewVBox(
		label,
		widget.NewForm(
			widget.NewFormItem("Mode", modeSelect),
			widget.NewFormItem("Num LEDs", container.NewBorder(nil, nil, nil, numLedsLabel, numLedsSlider)),
			widget.NewFormItem("Color 1", container.NewHBox(startColorRect, startColorBtn)),
			widget.NewFormItem("Color 2", container.NewHBox(endColorRect, endColorBtn)),
			widget.NewFormItem("Speed", container.NewBorder(nil, nil, nil, speedLabel, speedSlider)),
		),
	)
}

func colorToInt(c color.Color) uint32 {
	r, g, b, _ := c.RGBA()
	// RGBA returns 0-65535, we need 0-255
	r8 := uint32(r >> 8)
	g8 := uint32(g >> 8)
	b8 := uint32(b >> 8)
	return (r8 << 16) | (g8 << 8) | b8
}

func intToColor(i uint32) color.Color {
	r := uint8(i >> 16)
	g := uint8(i >> 8)
	b := uint8(i)
	return color.RGBA{R: r, G: g, B: b, A: 255}
}

func saveRgb() {
	payload := make(map[string]LedSettings)

	for id, widgets := range rgbWidgets {
		modeIdx := widgets.ModeSelect.SelectedIndex()
		if modeIdx == -1 {
			modeIdx = 0
		}

		payload[id] = LedSettings{
			Mode:       modeIdx,
			Speed:      int(widgets.SpeedSlider.Value),
			NumLeds:    int(widgets.NumLedsSlider.Value),
			StartColor: colorToInt(widgets.StartColorRect.FillColor),
			EndColor:   colorToInt(widgets.EndColorRect.FillColor),
		}
	}

	jsonBytes, err := json.Marshal(payload)
	if err != nil {
		dialog.ShowError(err, mainWindow)
		return
	}

	sendCommand("save-rgb " + string(jsonBytes))
}

func UpdateRgbUI() {
	rgbMutex.Lock()
	defer rgbMutex.Unlock()

	for id, settings := range rgbSettings {
		if widgets, ok := rgbWidgets[id]; ok {
			widgets.ModeSelect.SetSelectedIndex(settings.Mode)

			widgets.SpeedSlider.SetValue(float64(settings.Speed))
			widgets.SpeedLabel.SetText(fmt.Sprintf("%d", settings.Speed))

			widgets.NumLedsSlider.SetValue(float64(settings.NumLeds))
			widgets.NumLedsLabel.SetText(fmt.Sprintf("%d", settings.NumLeds))

			c1 := intToColor(settings.StartColor)
			widgets.StartColorRect.FillColor = c1
			widgets.StartColorRect.Refresh()

			c2 := intToColor(settings.EndColor)
			widgets.EndColorRect.FillColor = c2
			widgets.EndColorRect.Refresh()
		}
	}
}

func sendCommand(cmd string) {
	portMutex.Lock()
	defer portMutex.Unlock()
	if globalPort != nil {
		log.Printf("Sending command: %s", cmd)
		_, err := globalPort.Write([]byte(cmd + "\n"))
		if err != nil {
			log.Printf("Error sending command: %v", err)
		}
	} else {
		log.Println("Port not connected")
	}
}

func UpdateSettingsUI() {
	settingsMutex.Lock()
	s := currentSettings
	settingsMutex.Unlock()

	ssidEntry.SetText(s.SSID)
	// passwordEntry.SetText(s.Password) // Don't show password by default? Or do we? usually empty unless received.
	// The device sends password in get-settings response based on main.cpp.
	passwordEntry.SetText(s.Password)
	hostnameEntry.SetText(s.Hostname)

	unitsSelect.SetSelected(s.Units)
	offlineCheck.SetChecked(s.OfflineMode)

	switch s.FanPassthrough {
	case 0:
		fanPassthroughSel.SetSelected("FAN_PUMP")
	case 1:
		fanPassthroughSel.SetSelected("FAN_1")
	case 2:
		fanPassthroughSel.SetSelected("FAN_2")
	case 3:
		fanPassthroughSel.SetSelected("FAN_3")
	default:
		fanPassthroughSel.SetSelected("None")
	}

	mqttEnableCheck.SetChecked(s.MqttEnable)
	mqttBrokerEntry.SetText(s.MqttBroker)
	mqttTopicEntry.SetText(s.MqttTopic)
	mqttUserEntry.SetText(s.MqttUsername)
	mqttPassEntry.SetText(s.MqttPassword)
	mqttPortEntry.SetText(fmt.Sprintf("%d", s.MqttPort))
	telItvSelect.SetSelected(fmt.Sprintf("%d", s.TelemetryItv))

	// Trigger enable/disable logic
	if s.MqttEnable {
		mqttBrokerEntry.Enable()
		mqttTopicEntry.Enable()
		mqttUserEntry.Enable()
		mqttPassEntry.Enable()
		mqttPortEntry.Enable()
		telItvSelect.Enable()
	} else {
		mqttBrokerEntry.Disable()
		mqttTopicEntry.Disable()
		mqttUserEntry.Disable()
		mqttPassEntry.Disable()
		mqttPortEntry.Disable()
		telItvSelect.Disable()
	}
}

func startTelemetryMonitor() {
	var port serial.Port
	var staleRetries int = 0

OUTER:
	for {
		// Find the COM port
		portName, err := findDeviceComPort(VID, PID)
		if err != nil {
			log.Printf("Error finding device: %v. Retrying in 5 seconds...", err)
			time.Sleep(5 * time.Second)
			continue
		}

		// Open the serial port
		port, err = serial.Open(portName, &serial.Mode{BaudRate: 115200, DataBits: 8, Parity: serial.NoParity, StopBits: 1})
		if err != nil {
			log.Printf("Error opening serial port %s: %v. Retrying in 5 seconds...", portName, err)
			time.Sleep(5 * time.Second)
			continue
		}

		portMutex.Lock()
		globalPort = port
		portMutex.Unlock()

		log.Printf("Successfully opened serial port %s", portName)
		port.SetReadTimeout(ComPortTimeout)

		reader := make([]byte, 4096)
		var jsonBuffer []byte

		for {
			n, err := port.Read(reader)
			if err != nil {
				if err.Error() == "The I/O operation has been aborted because of either a thread exit or an application request." {
					log.Printf("Serial port %s disconnected. Attempting to reconnect...", portName)
					port.Close()

					portMutex.Lock()
					globalPort = nil
					portMutex.Unlock()

					break
				}
				log.Printf("Error reading from serial port %s: %v", portName, err)
				time.Sleep(1 * time.Second)
			}

			if n == 0 {
				staleRetries++
				time.Sleep(100 * time.Millisecond)
				if staleRetries > 10 {
					log.Printf("Retrying %d", staleRetries)
					staleRetries = 0
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
					jsonBuffer = []byte{}
					break
				}

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
					jsonStr := string(jsonBuffer[startIndex : endIndex+1])
					go processTelemetry(jsonStr)
					jsonBuffer = jsonBuffer[endIndex+1:]
					staleRetries = 0
				} else {
					break
				}
			}
		}
	}
}

func findDeviceComPort(vid, pid string) (string, error) {
	ports, err := enumerator.GetDetailedPortsList()
	if err != nil {
		return "", err
	}
	for _, port := range ports {
		if port.IsUSB {
			if strings.EqualFold(port.VID, vid) && strings.EqualFold(port.PID, pid) {
				return port.Name, nil
			}
		}
	}
	return "", fmt.Errorf("waku controller device with VID %s and PID %s not found", vid, pid)
}

func processTelemetry(jsonStr string) {
	var telemetry TelemetryData
	err := json.Unmarshal([]byte(jsonStr), &telemetry)
	if err != nil {
		log.Printf("Error unmarshalling JSON: %v", err)
		return
	}

	// Check if it's telemetry or settings
	if telemetry.Event != "" {
		// Is Telemetry
		dataMutex.Lock()
		for i, sensor := range sensorInfos {
			if i < len(sensorData) {
				val := sensor.getValue(telemetry)
				unit := sensor.defaultUnit
				if sensor.subKeyName == "Temp0" || sensor.subKeyName == "Temp1" {
					unit = getTempUnit(telemetry)
				}

				sensorData[i].Value = val
				sensorData[i].Unit = unit
			}
		}
		dataMutex.Unlock()

		// Refresh UI
		fyne.Do(func() {
			dataList.Refresh()
		})

		// Update Registry (Existing Logic)
		updateRegistry(telemetry)
	} else {
		// Try Settings or RGB
		// We can check if it has "LED_0" key to identify RGB settings
		// Or try to unmarshal to map[string]LedSettings

		// Quick check string for "LED_"
		if strings.Contains(jsonStr, `"LED_`) {
			// Assume RGB (very naive check but likely effective given our control)
			var rgb map[string]LedSettings
			err := json.Unmarshal([]byte(jsonStr), &rgb)
			if err == nil {
				rgbMutex.Lock()
				// Update existing map
				for k, v := range rgb {
					rgbSettings[k] = v
				}
				rgbMutex.Unlock()

				fyne.Do(func() {
					UpdateRgbUI()
				})
				return
			}
		}

		// Try Settings
		var settings Settings
		err := json.Unmarshal([]byte(jsonStr), &settings)
		if err == nil {
			settingsMutex.Lock()
			// Only update if it looks valid (e.g. Hostname or SSID is present, or just assume)
			// Actually empty JSON "{}" unmarshals to empty struct without error.
			// However `get-settings` returns full object.
			currentSettings = settings
			settingsMutex.Unlock()

			fyne.Do(func() {
				UpdateSettingsUI()
			})
		}
	}
}

func updateRegistry(telemetry TelemetryData) {
	mainAppKey, _, err := registry.CreateKey(registry.CURRENT_USER, RegistryKey, registry.CREATE_SUB_KEY)
	if err != nil {
		log.Printf("Error creating/opening main registry key: %v", err)
		return
	}
	defer mainAppKey.Close()

	for _, sensor := range sensorInfos {
		sensorKey, _, err := registry.CreateKey(mainAppKey, sensor.subKeyName, registry.SET_VALUE)
		if err != nil {
			continue
		}

		valueStr := sensor.getValue(telemetry)
		unitStr := sensor.defaultUnit
		if sensor.subKeyName == "Temp0" || sensor.subKeyName == "Temp1" {
			unitStr = getTempUnit(telemetry)
		}

		sensorKey.SetStringValue("Name", sensor.displayName)
		sensorKey.SetStringValue("Value", valueStr)
		sensorKey.SetStringValue("Unit", unitStr)
		sensorKey.Close()
	}
}
