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
	"fyne.io/fyne/v2/driver/desktop"
	"fyne.io/fyne/v2/layout"
	"fyne.io/fyne/v2/theme"
	"fyne.io/fyne/v2/widget"
	"go.bug.st/serial"
	"go.bug.st/serial/enumerator"
)

const (
	VID            = "303A"
	PID            = "82E5"
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
			DeltaTemp12  float64 `json:"DELTA_T1_T2"`
			DeltaTemp13  float64 `json:"DELTA_T1_T3"`
			DeltaTemp23  float64 `json:"DELTA_T2_T3"`
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
	ScreenRotation int    `json:"screen_rotation"`
}

// LedSettings represents the configuration for an RGB Strip
type LedSettings struct {
	Mode       int    `json:"mode"`
	Speed      int    `json:"speed"`
	StartColor uint32 `json:"start_color"`
	EndColor   uint32 `json:"end_color"`
	NumLeds    int    `json:"num_leds"`
}

type FanCurvePoint struct {
	Temp float64 `json:"temp"`
	Fan  int     `json:"fan"`
}

type FanConfig struct {
	Sensor      int             `json:"sensor"`
	TempTh      int             `json:"temp_th"`
	DutyTh      int             `json:"duty_th"`
	SudDur      int             `json:"sud_dur"`
	HaltOn      int             `json:"halt_on"`
	Units       string          `json:"units"`
	Mode        int             `json:"mode"`
	PidKp       float64         `json:"pid_kp"`
	PidKi       float64         `json:"pid_ki"`
	PidKd       float64         `json:"pid_kd"`
	PidSetpoint float64         `json:"pid_setpoint"`
	Curves      []FanCurvePoint `json:"curves"`
}

type SensorsResponse struct {
	Sensors []string `json:"sensors"`
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

	// Fans Settings
	fanConfigs      map[string]FanConfig
	fanConfigsMutex sync.Mutex

	// Available Sensors from device
	availableSensors      []string
	availableSensorsMutex sync.Mutex

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
	screenRotCheck    *widget.Check

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

	overlayText *widget.Label

	mainWindow fyne.Window

	connectionOverlay *fyne.Container

	// Define sensor configurations
	sensorInfos = []sensorRegistryInfo{
		{"Temp0", "Temperature sensor 0", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature1) }},
		{"Temp1", "Temperature sensor 1", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature2) }},
		{"Temp2", "Temperature sensor 2", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.Temperature3) }},
		{"DTemp12", "Temperature sensor delta 1-2", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.DeltaTemp12) }},
		{"DTemp13", "Temperature sensor delta 1-3", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.DeltaTemp13) }},
		{"DTemp23", "Temperature sensor delta 2-3", "°C", func(td TelemetryData) string { return fmt.Sprintf("%2.f", td.Data.Temps.DeltaTemp23) }},
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
	a := app.NewWithID("com.kennyslabs.wakuctl")
	w := a.NewWindow("Waku Controller")
	mainWindow = w

	// Initialize sensorData with empty values based on sensorInfos
	for _, info := range sensorInfos {
		sensorData = append(sensorData, SensorDisplay{
			Name:  info.displayName,
			Value: "-",
			Unit:  info.defaultUnit,
		})
	}

	// Create Tabs Content
	homeContent := makeHomeTab()
	curvesContent := makeCurvesTab()
	rgbContent := makeRgbTab()
	settingsContent := makeSettingsTab()

	contentStack := container.NewStack(homeContent)

	menuItems := []string{"Home", "Curves", "RGB", "Settings", "Quit"}
	menuList := widget.NewList(
		func() int { return len(menuItems) },
		func() fyne.CanvasObject { return widget.NewLabel("Menu Item") },
		func(id widget.ListItemID, o fyne.CanvasObject) {
			o.(*widget.Label).SetText(menuItems[id])
		},
	)
	menuList.OnSelected = func(id widget.ListItemID) {
		contentStack.Objects = []fyne.CanvasObject{}
		switch id {
		case 0:
			contentStack.Add(homeContent)
		case 1:
			contentStack.Add(curvesContent)
			sendCommand("get-sensors")
			time.Sleep(100 * time.Millisecond)
			sendCommand("get-curves")
		case 2:
			contentStack.Add(rgbContent)
			sendCommand("get-rgb")
		case 3:
			contentStack.Add(settingsContent)
			sendCommand("get-settings")
		case 4:
			quitApp(a)
		}
		contentStack.Refresh()
	}
	menuList.Select(0)

	// Sidebar with slightly wider width
	sidebar := container.NewMax(menuList)
	sidebarContainer := container.NewHBox(container.NewGridWrap(fyne.NewSize(150, 600), sidebar))

	split := container.NewHSplit(sidebarContainer, contentStack)
	split.Offset = 0.2 // Initial split ratio

	// Connection Error Overlay
	overlayText = widget.NewLabelWithStyle("Establishing connection to waku-ctl, please wait...", fyne.TextAlignCenter, fyne.TextStyle{Bold: true})
	overlayIcon := widget.NewIcon(theme.WarningIcon())
	overlayContent := container.NewVBox(
		layout.NewSpacer(),
		container.NewCenter(container.NewVBox(overlayIcon, overlayText)),
		layout.NewSpacer(),
	)
	overlayBg := canvas.NewRectangle(color.NRGBA{R: 0, G: 0, B: 0, A: 180})
	connectionOverlay = container.NewStack(overlayBg, overlayContent)

	mainStack := container.NewStack(split, connectionOverlay)
	w.SetContent(mainStack)
	w.Resize(fyne.NewSize(900, 600)) // Increased size for wider menu
	w.SetFixedSize(true)             // Non-resizable

	// Tray Menu
	if desk, ok := a.(desktop.App); ok {
		m := fyne.NewMenu("Waku Controller",
			fyne.NewMenuItem("Open", func() {
				w.Show()
			}),
			fyne.NewMenuItemSeparator(),
			fyne.NewMenuItem("Quit", func() {
				quitApp(a)
			}),
		)
		desk.SetSystemTrayMenu(m)

		// Tray Icon
		iconPath := "tray.png" // Should be in the same directory as the executable or adjusted
		if resource, err := fyne.LoadResourceFromPath(iconPath); err == nil {
			desk.SetSystemTrayIcon(resource)
		} else {
			log.Printf("Failed to load tray icon %s: %v", iconPath, err)
			// Fallback to app icon or theme icon if tray.png is missing
			desk.SetSystemTrayIcon(theme.SettingsIcon())
		}
	}

	// Intercept close to hide to tray
	w.SetCloseIntercept(func() {
		w.Hide()
	})

	go startTelemetryMonitor()

	w.ShowAndRun()
}

func quitApp(a fyne.App) {
	log.Println("Quitting application...")

	// Cleanup the registry for HWInfo64
	var telemetry TelemetryData
	_ = json.Unmarshal([]byte("{\"temperature1\":0, \"temperature2\":0, \"temperature3\":0, \"FAN0\":0, \"FAN1\":0, \"FAN2\":0, \"FAN3\":0}"), &telemetry)
	updateRegistry(telemetry)

	// Close serial port
	portMutex.Lock()
	if globalPort != nil {
		log.Println("Closing serial port...")
		globalPort.Close()
		globalPort = nil
	}
	portMutex.Unlock()

	a.Quit()
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

	copyTo2Btn := widget.NewButton("Copy to Header #2 \u2193", func() {
		copyRgbSettings("LED_0", "LED_1")
	})
	copyTo1Btn := widget.NewButton("Copy to Header #1 \u2191", func() {
		copyRgbSettings("LED_1", "LED_0")
	})

	copyContainer := container.NewCenter(container.NewHBox(copyTo2Btn, copyTo1Btn))

	saveBtn := widget.NewButton("Save Colors", func() {
		saveRgb()
	})

	return container.NewBorder(nil, container.NewHBox(saveBtn), nil, nil, container.NewVScroll(container.NewVBox(led0, widget.NewSeparator(), copyContainer, widget.NewSeparator(), led1)))
}

func copyRgbSettings(srcID, dstID string) {
	src, okSrc := rgbWidgets[srcID]
	dst, okDst := rgbWidgets[dstID]

	if !okSrc || !okDst {
		return
	}

	dst.ModeSelect.SetSelectedIndex(src.ModeSelect.SelectedIndex())

	dst.SpeedSlider.SetValue(src.SpeedSlider.Value)
	dst.SpeedLabel.SetText(fmt.Sprintf("%d", int(src.SpeedSlider.Value)))

	dst.NumLedsSlider.SetValue(src.NumLedsSlider.Value)
	dst.NumLedsLabel.SetText(fmt.Sprintf("%d", int(src.NumLedsSlider.Value)))

	dst.StartColorRect.FillColor = src.StartColorRect.FillColor
	dst.StartColorRect.Refresh()

	dst.EndColorRect.FillColor = src.EndColorRect.FillColor
	dst.EndColorRect.Refresh()
}

func makeSettingsTab() fyne.CanvasObject {
	ssidEntry = widget.NewEntry()
	passwordEntry = widget.NewPasswordEntry()
	hostnameEntry = widget.NewEntry()

	unitsSelect = widget.NewSelect([]string{"C", "F"}, nil)
	unitsSelect.SetSelected("C")

	offlineCheck = widget.NewCheck("Offline Mode (AP Only)", nil)

	fanPassthroughSel = widget.NewSelect([]string{"FAN_PUMP", "FAN_1", "FAN_2", "FAN_3", "None"}, nil)

	screenRotCheck = widget.NewCheck("Flip Display 180°", nil)

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
		widget.NewFormItem("Display Setup", layout.NewSpacer()),
		widget.NewFormItem("", screenRotCheck),
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

	return container.NewBorder(nil, container.NewHBox(saveBtn), nil, nil, container.NewVScroll(form))
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

	if screenRotCheck.Checked {
		s.ScreenRotation = 2
	} else {
		s.ScreenRotation = 0
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

	settingsMutex.Lock()
	currentSettings = s
	settingsMutex.Unlock()

	fyne.Do(func() {
		UpdateCurvesUI()
	})

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

func UpdateSettingsUI() {
	log.Println("Updating Settings UI...")
	settingsMutex.Lock()
	s := currentSettings
	settingsMutex.Unlock()

	if ssidEntry != nil {
		ssidEntry.SetText(s.SSID)
	}
	if passwordEntry != nil {
		passwordEntry.SetText(s.Password)
	}
	if hostnameEntry != nil {
		hostnameEntry.SetText(s.Hostname)
	}

	if unitsSelect != nil {
		unitsSelect.SetSelected(s.Units)
	}
	if offlineCheck != nil {
		offlineCheck.SetChecked(s.OfflineMode)
	}

	if fanPassthroughSel != nil {
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
	}

	if screenRotCheck != nil {
		screenRotCheck.SetChecked(s.ScreenRotation == 2)
	}

	if mqttEnableCheck != nil {
		mqttEnableCheck.SetChecked(s.MqttEnable)
	}
	if mqttBrokerEntry != nil {
		mqttBrokerEntry.SetText(s.MqttBroker)
	}
	if mqttTopicEntry != nil {
		mqttTopicEntry.SetText(s.MqttTopic)
	}
	if mqttUserEntry != nil {
		mqttUserEntry.SetText(s.MqttUsername)
	}
	if mqttPassEntry != nil {
		mqttPassEntry.SetText(s.MqttPassword)
	}
	if mqttPortEntry != nil {
		mqttPortEntry.SetText(fmt.Sprintf("%d", s.MqttPort))
	}
	if telItvSelect != nil {
		telItvSelect.SetSelected(fmt.Sprintf("%d", s.TelemetryItv))
	}

	// Trigger enable/disable logic
	if mqttEnableCheck != nil {
		if s.MqttEnable {
			if mqttBrokerEntry != nil {
				mqttBrokerEntry.Enable()
			}
			if mqttTopicEntry != nil {
				mqttTopicEntry.Enable()
			}
			if mqttUserEntry != nil {
				mqttUserEntry.Enable()
			}
			if mqttPassEntry != nil {
				mqttPassEntry.Enable()
			}
			if mqttPortEntry != nil {
				mqttPortEntry.Enable()
			}
			if telItvSelect != nil {
				telItvSelect.Enable()
			}
		} else {
			if mqttBrokerEntry != nil {
				mqttBrokerEntry.Disable()
			}
			if mqttTopicEntry != nil {
				mqttTopicEntry.Disable()
			}
			if mqttUserEntry != nil {
				mqttUserEntry.Disable()
			}
			if mqttPassEntry != nil {
				mqttPassEntry.Disable()
			}
			if mqttPortEntry != nil {
				mqttPortEntry.Disable()
			}
			if telItvSelect != nil {
				telItvSelect.Disable()
			}
		}
	}
}

func UpdateRgbUI() {
	log.Println("Updating RGB UI...")
	rgbMutex.Lock()
	defer rgbMutex.Unlock()

	if rgbWidgets == nil {
		log.Println("UpdateRgbUI error: rgbWidgets map is nil")
		return
	}

	for id, settings := range rgbSettings {
		if widgets, ok := rgbWidgets[id]; ok {
			log.Printf("Updating RGB widgets for %s", id)
			if widgets.ModeSelect != nil {
				widgets.ModeSelect.SetSelectedIndex(settings.Mode)
			}

			if widgets.SpeedSlider != nil {
				widgets.SpeedSlider.SetValue(float64(settings.Speed))
			}
			if widgets.SpeedLabel != nil {
				widgets.SpeedLabel.SetText(fmt.Sprintf("%d", settings.Speed))
			}

			if widgets.NumLedsSlider != nil {
				widgets.NumLedsSlider.SetValue(float64(settings.NumLeds))
			}
			if widgets.NumLedsLabel != nil {
				widgets.NumLedsLabel.SetText(fmt.Sprintf("%d", settings.NumLeds))
			}

			c1 := intToColor(settings.StartColor)
			if widgets.StartColorRect != nil {
				widgets.StartColorRect.FillColor = c1
				widgets.StartColorRect.Refresh()
			}

			c2 := intToColor(settings.EndColor)
			if widgets.EndColorRect != nil {
				widgets.EndColorRect.FillColor = c2
				widgets.EndColorRect.Refresh()
			}
		}
	}
}

// --- Curves Tab Implementation ---

var fanWidgets map[string]struct {
	ModeSegment    *widget.Select
	CurveContainer *fyne.Container
	PidContainer   *fyne.Container
	// Common
	TempSourceSelect *widget.Select
	TempAlarmSelect  *widget.Select
	FanAlarmSelect   *widget.Select
	HaltOnSelect     *widget.Select
	StepDurSlider    *widget.Slider
	// PID
	PidTargetEntry *widget.Entry
	PidKpEntry     *widget.Entry
	PidKiEntry     *widget.Entry
	PidKdEntry     *widget.Entry
	// Curves (pointers to entries/sliders)
	CurvePoints []struct {
		TempSlider *widget.Slider
		FanSlider  *widget.Slider
		TempLabel  *widget.Label
		FanLabel   *widget.Label
	}
}

func makeCurvesTab() fyne.CanvasObject {
	fanWidgets = make(map[string]struct {
		ModeSegment      *widget.Select
		CurveContainer   *fyne.Container
		PidContainer     *fyne.Container
		TempSourceSelect *widget.Select
		TempAlarmSelect  *widget.Select
		FanAlarmSelect   *widget.Select
		HaltOnSelect     *widget.Select
		StepDurSlider    *widget.Slider
		PidTargetEntry   *widget.Entry
		PidKpEntry       *widget.Entry
		PidKiEntry       *widget.Entry
		PidKdEntry       *widget.Entry
		CurvePoints      []struct {
			TempSlider *widget.Slider
			FanSlider  *widget.Slider
			TempLabel  *widget.Label
			FanLabel   *widget.Label
		}
	})

	// Initialize default map
	fanConfigs = make(map[string]FanConfig)
	for i := 0; i < 4; i++ {
		fanID := fmt.Sprintf("FAN_%d", i)
		fanConfigs[fanID] = FanConfig{
			Units: "C",
			Curves: []FanCurvePoint{
				{30, 0}, {40, 30}, {50, 60}, {60, 80}, {70, 100},
			},
		}
	}

	fan0 := makeFanSection("FAN_0", "Fan Pump / Header #0")
	fan1 := makeFanSection("FAN_1", "Fan Header #1")
	fan2 := makeFanSection("FAN_2", "Fan Header #2")
	fan3 := makeFanSection("FAN_3", "Fan Header #3")

	saveBtn := widget.NewButton("Save Curves", func() {
		saveCurves()
	})

	return container.NewBorder(nil, container.NewHBox(saveBtn), nil, nil,
		container.NewVScroll(container.NewVBox(
			fan0, widget.NewSeparator(),
			fan1, widget.NewSeparator(),
			fan2, widget.NewSeparator(),
			fan3,
		)))
}

func makeFanSection(id string, title string) fyne.CanvasObject {
	label := widget.NewLabelWithStyle(title, fyne.TextAlignLeading, fyne.TextStyle{Bold: true})

	// Common Settings
	availableSensorsMutex.Lock()
	opts := availableSensors
	availableSensorsMutex.Unlock()
	if len(opts) == 0 {
		opts = []string{"Sensor 0", "Sensor 1", "Sensor 2"} // Default fallback
	}
	tempSource := widget.NewSelect(opts, nil)

	settingsMutex.Lock()
	unit := currentSettings.Units
	if unit == "" {
		unit = "C"
	}
	settingsMutex.Unlock()

	tempAlarm := widget.NewSelect([]string{"No alarm", ">= 30°" + unit, ">= 40°" + unit, ">= 50°" + unit, ">= 60°" + unit, ">= 70°" + unit, ">= 80°" + unit, ">= 90°" + unit}, nil)
	fanAlarm := widget.NewSelect([]string{"No alarm", "< 100 RPM", "< 200 RPM", "< 300 RPM", "< 500 RPM"}, nil)

	haltOn := widget.NewSelect([]string{"No halt", "Halt on fan speed alarm", "Halt on temperature alarm", "Halt on both"}, nil)

	stepDurLabel := widget.NewLabel("1 sec")
	stepDur := widget.NewSlider(1, 100)
	stepDur.OnChanged = func(f float64) {
		stepDurLabel.SetText(fmt.Sprintf("%d sec", int(f)))
	}

	commonForm := widget.NewForm(
		widget.NewFormItem("Temp Source", tempSource),
		widget.NewFormItem("Temp Alarm", tempAlarm),
		widget.NewFormItem("Fan Alarm", fanAlarm),
		widget.NewFormItem("Halt On", haltOn),
		widget.NewFormItem("Step Duration", container.NewBorder(nil, nil, nil, stepDurLabel, stepDur)),
	)

	// Mode Selection
	modeSelect := widget.NewSelect([]string{"Curve", "PID"}, nil)
	modeSelect.SetSelected("Curve")

	// Curve Editor
	curveContainer := container.NewVBox()
	var curvePoints []struct {
		TempSlider *widget.Slider
		FanSlider  *widget.Slider
		TempLabel  *widget.Label
		FanLabel   *widget.Label
	}

	for i := 0; i < 5; i++ {
		tLabel := widget.NewLabel(fmt.Sprintf("P%d Temp", i+1))
		tSlider := widget.NewSlider(0, 100) // 0-100 C
		tValLabel := widget.NewLabel("30°" + unit)
		tSlider.OnChanged = func(f float64) {
			settingsMutex.Lock()
			u := currentSettings.Units
			if u == "" {
				u = "C"
			}
			settingsMutex.Unlock()
			if u == "C" {
				tValLabel.SetText(fmt.Sprintf("%d°%s", int(f), u))
			} else {
				tValLabel.SetText(fmt.Sprintf("%d°%s", int((f*9/5)+32), u))
			}
		}

		fLabel := widget.NewLabel(fmt.Sprintf("P%d Fan", i+1))
		fSlider := widget.NewSlider(0, 255) // 0-255 PWM
		fValLabel := widget.NewLabel("0%")
		fSlider.OnChanged = func(f float64) { fValLabel.SetText(fmt.Sprintf("%d%%", int(f/2.55))) }

		row := container.NewGridWithColumns(2,
			container.NewBorder(nil, nil, tLabel, tValLabel, tSlider),
			container.NewBorder(nil, nil, fLabel, fValLabel, fSlider),
		)
		curveContainer.Add(row)

		curvePoints = append(curvePoints, struct {
			TempSlider *widget.Slider
			FanSlider  *widget.Slider
			TempLabel  *widget.Label
			FanLabel   *widget.Label
		}{tSlider, fSlider, tValLabel, fValLabel})
	}

	// PID Editor
	pidContainer := container.NewVBox()
	pidTarget := widget.NewEntry()
	pidTarget.SetText("30")
	pidKp := widget.NewEntry()
	pidKp.SetText("1.0")
	pidKi := widget.NewEntry()
	pidKi.SetText("0.1")
	pidKd := widget.NewEntry()
	pidKd.SetText("0.5")

	pidContainer.Add(widget.NewForm(
		widget.NewFormItem("Target Temp", pidTarget),
		widget.NewFormItem("Kp (Reaction)", pidKp),
		widget.NewFormItem("Ki (Correction)", pidKi),
		widget.NewFormItem("Kd (Stability)", pidKd),
	))
	pidContainer.Hide() // Default hidden

	// Mode Logic
	modeSelect.OnChanged = func(s string) {
		if s == "Curve" {
			curveContainer.Show()
			pidContainer.Hide()
		} else {
			curveContainer.Hide()
			pidContainer.Show()
		}
	}

	// Store widgets
	fanWidgets[id] = struct {
		ModeSegment      *widget.Select
		CurveContainer   *fyne.Container
		PidContainer     *fyne.Container
		TempSourceSelect *widget.Select
		TempAlarmSelect  *widget.Select
		FanAlarmSelect   *widget.Select
		HaltOnSelect     *widget.Select
		StepDurSlider    *widget.Slider
		PidTargetEntry   *widget.Entry
		PidKpEntry       *widget.Entry
		PidKiEntry       *widget.Entry
		PidKdEntry       *widget.Entry
		CurvePoints      []struct {
			TempSlider *widget.Slider
			FanSlider  *widget.Slider
			TempLabel  *widget.Label
			FanLabel   *widget.Label
		}
	}{
		ModeSegment:      modeSelect,
		CurveContainer:   curveContainer,
		PidContainer:     pidContainer,
		TempSourceSelect: tempSource,
		TempAlarmSelect:  tempAlarm,
		FanAlarmSelect:   fanAlarm,
		HaltOnSelect:     haltOn,
		StepDurSlider:    stepDur,
		PidTargetEntry:   pidTarget,
		PidKpEntry:       pidKp,
		PidKiEntry:       pidKi,
		PidKdEntry:       pidKd,
		CurvePoints:      curvePoints,
	}

	return container.NewVBox(
		label,
		commonForm,
		widget.NewForm(widget.NewFormItem("Mode", modeSelect)),
		curveContainer,
		pidContainer,
	)
}

func saveCurves() {
	// Construct Payload
	payload := make(map[string]FanConfig)

	for id, w := range fanWidgets {
		cfg := FanConfig{}

		// Common
		cfg.Sensor = w.TempSourceSelect.SelectedIndex()
		if cfg.Sensor == -1 {
			cfg.Sensor = 0
		}

		// Map Alarms back to values
		// Temp Alarm
		taStr := w.TempAlarmSelect.Selected
		if taStr == "No alarm" {
			cfg.TempTh = 999
		} else {
			fmt.Sscanf(taStr, ">= %d", &cfg.TempTh)
		}

		// Fan Alarm
		faStr := w.FanAlarmSelect.Selected
		if faStr == "No alarm" {
			cfg.DutyTh = -1
		} else {
			fmt.Sscanf(faStr, "< %d", &cfg.DutyTh)
		}

		cfg.HaltOn = w.HaltOnSelect.SelectedIndex()
		cfg.SudDur = int(w.StepDurSlider.Value)

		// Mode
		if w.ModeSegment.Selected == "Curve" {
			cfg.Mode = 0
		} else {
			cfg.Mode = 1
		}

		// PID
		unit := currentSettings.Units
		fmt.Sscanf(w.PidTargetEntry.Text, "%f", &cfg.PidSetpoint)
		if unit == "F" {
			cfg.PidSetpoint = (cfg.PidSetpoint - 32) * 5 / 9
		}

		fmt.Sscanf(w.PidKpEntry.Text, "%f", &cfg.PidKp)
		fmt.Sscanf(w.PidKiEntry.Text, "%f", &cfg.PidKi)
		fmt.Sscanf(w.PidKdEntry.Text, "%f", &cfg.PidKd)

		// Curves
		for _, p := range w.CurvePoints {
			cfg.Curves = append(cfg.Curves, FanCurvePoint{
				Temp: p.TempSlider.Value,
				Fan:  int(p.FanSlider.Value),
			})
		}

		payload[id] = cfg
	}

	jsonBytes, err := json.Marshal(payload)
	if err != nil {
		dialog.ShowError(err, mainWindow)
		return
	}
	sendCommand("save-curves " + string(jsonBytes))
}

func UpdateCurvesUI() {
	log.Println("Updating Curves UI...")
	fanConfigsMutex.Lock()
	defer fanConfigsMutex.Unlock()

	if fanWidgets == nil {
		log.Println("UpdateCurvesUI error: fanWidgets map is nil")
		return
	}

	for id, cfg := range fanConfigs {
		w, ok := fanWidgets[id]
		if !ok {
			log.Printf("UpdateCurvesUI: No fan widgets for %s", id)
			continue
		}

		settingsMutex.Lock()
		unit := currentSettings.Units
		if unit == "" {
			unit = "C"
		}
		settingsMutex.Unlock()

		log.Printf("UpdateCurvesUI: %s - sensor=%d, mode=%d", id, cfg.Sensor, cfg.Mode)

		// Populate Common
		if w.TempSourceSelect != nil {
			availableSensorsMutex.Lock()
			if len(availableSensors) > 0 {
				w.TempSourceSelect.Options = availableSensors
			}
			availableSensorsMutex.Unlock()

			if cfg.Sensor >= 0 && cfg.Sensor < len(w.TempSourceSelect.Options) {
				w.TempSourceSelect.SetSelectedIndex(cfg.Sensor)
			}
			w.TempSourceSelect.Refresh()
		}

		// Temp Alarm
		if w.TempAlarmSelect != nil {
			settingsMutex.Lock()
			unit := currentSettings.Units
			if unit == "" {
				unit = "C"
			}
			settingsMutex.Unlock()

			// Update options if unit changed
			if unit == "C" {
				w.TempAlarmSelect.Options = []string{"No alarm",
					fmt.Sprintf(">= 30°%s", unit),
					fmt.Sprintf(">= 40°%s", unit),
					fmt.Sprintf(">= 50°%s", unit),
					fmt.Sprintf(">= 60°%s", unit),
					fmt.Sprintf(">= 70°%s", unit),
					fmt.Sprintf(">= 80°%s", unit),
					fmt.Sprintf(">= 90°%s", unit),
				}
			} else {
				w.TempAlarmSelect.Options = []string{"No alarm",
					fmt.Sprintf(">= 86°%s", unit),
					fmt.Sprintf(">= 104°%s", unit),
					fmt.Sprintf(">= 122°%s", unit),
					fmt.Sprintf(">= 140°%s", unit),
					fmt.Sprintf(">= 158°%s", unit),
					fmt.Sprintf(">= 176°%s", unit),
					fmt.Sprintf(">= 194°%s", unit),
				}
			}

			if cfg.TempTh >= 999 {
				w.TempAlarmSelect.SetSelected("No alarm")
			} else {
				w.TempAlarmSelect.SetSelected(fmt.Sprintf(">= %d°%s", cfg.TempTh, unit))
			}
		}

		// Fan Alarm
		if w.FanAlarmSelect != nil {
			if cfg.DutyTh <= -1 {
				w.FanAlarmSelect.SetSelected("No alarm")
			} else {
				w.FanAlarmSelect.SetSelected(fmt.Sprintf("< %d RPM", cfg.DutyTh))
			}
		}

		if w.HaltOnSelect != nil {
			w.HaltOnSelect.SetSelectedIndex(cfg.HaltOn)
		}

		if w.StepDurSlider != nil {
			w.StepDurSlider.SetValue(float64(cfg.SudDur))
		}

		// Mode
		if w.ModeSegment != nil {
			if cfg.Mode == 0 {
				w.ModeSegment.SetSelected("Curve")
				if w.CurveContainer != nil {
					w.CurveContainer.Show()
				}
				if w.PidContainer != nil {
					w.PidContainer.Hide()
				}
			} else {
				w.ModeSegment.SetSelected("PID")
				if w.CurveContainer != nil {
					w.CurveContainer.Hide()
				}
				if w.PidContainer != nil {
					w.PidContainer.Show()
				}
			}
		}

		// PID
		if w.PidTargetEntry != nil {
			if unit == "C" {
				w.PidTargetEntry.SetText(fmt.Sprintf("%.1f", cfg.PidSetpoint))
			} else {
				w.PidTargetEntry.SetText(fmt.Sprintf("%.1f", (cfg.PidSetpoint*9/5)+32))
			}
		}
		if w.PidKpEntry != nil {
			w.PidKpEntry.SetText(fmt.Sprintf("%.1f", cfg.PidKp))
		}
		if w.PidKiEntry != nil {
			w.PidKiEntry.SetText(fmt.Sprintf("%.1f", cfg.PidKi))
		}
		if w.PidKdEntry != nil {
			w.PidKdEntry.SetText(fmt.Sprintf("%.1f", cfg.PidKd))
		}

		// Curves
		log.Printf("UpdateCurvesUI: %s - populating %d curve points", id, len(cfg.Curves))
		for i, p := range cfg.Curves {
			if i < len(w.CurvePoints) {
				if w.CurvePoints[i].TempSlider != nil {
					w.CurvePoints[i].TempSlider.SetValue(p.Temp)
					if w.CurvePoints[i].TempLabel != nil {
						if unit == "C" {
							w.CurvePoints[i].TempLabel.SetText(fmt.Sprintf("%d°%s", int(p.Temp), unit))
						} else {
							w.CurvePoints[i].TempLabel.SetText(fmt.Sprintf("%d°%s", int((p.Temp*9/5)+32), unit))
						}
					}
				}
				if w.CurvePoints[i].FanSlider != nil {
					w.CurvePoints[i].FanSlider.SetValue(float64(p.Fan))
				}
			}
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

func startTelemetryMonitor() {
	var port serial.Port
	var staleRetries int = 0

OUTER:
	for {
		fyne.Do(func() {
			if connectionOverlay != nil {
				connectionOverlay.Show()
				connectionOverlay.Refresh()
			}
		})

		// Find the COM port
		portName, err := findDeviceComPort(VID, PID)
		if err != nil {
			overlayText.SetText("Error finding device, please connect and try again")
			connectionOverlay.Refresh()
			log.Printf("Error finding device: %v. Retrying in 5 seconds...", err)
			time.Sleep(5 * time.Second)
			continue
		}

		// // Perform a dummy connection cycle to stabilize the driver/device
		// if dPort, err := serial.Open(portName, &serial.Mode{BaudRate: 115200, DataBits: 8, Parity: serial.NoParity, StopBits: 1}); err == nil {
		// 	dPort.SetRTS(true)
		// 	time.Sleep(200 * time.Millisecond)
		// 	dPort.Close()
		// 	time.Sleep(200 * time.Millisecond)
		// }

		// Open the serial port for real
		port, err = serial.Open(portName, &serial.Mode{BaudRate: 115200, DataBits: 8, Parity: serial.NoParity, StopBits: 1})
		if err != nil {
			overlayText.SetText("Error opening serial port, please try again")
			connectionOverlay.Refresh()
			log.Printf("Error opening serial port %s: %v. Retrying in 5 seconds...", portName, err)
			time.Sleep(5 * time.Second)
			continue
		}

		portMutex.Lock()
		globalPort = port
		portMutex.Unlock()

		log.Printf("Successfully opened serial port %s", portName)
		port.SetReadTimeout(ComPortTimeout)

		// Set DTR and RTS high - required for many ESP32 CDC implementations
		port.SetDTR(true)
		port.SetRTS(true)

		// Send initial commands to fetch current state
		// No commands here as requested

		reader := make([]byte, 4096)
		var jsonBuffer []byte

		sendCommand("get-settings")

		for {
			n, err := port.Read(reader)
			if err != nil {
				fyne.Do(func() {
					if connectionOverlay != nil {
						connectionOverlay.Show()
						connectionOverlay.Refresh()
					}
				})

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

			if n > 0 {
				fyne.Do(func() {
					if connectionOverlay != nil {
						connectionOverlay.Hide()
						connectionOverlay.Refresh()
					}
				})
				staleRetries = 0 // Reset stale retries on successful read
			} else { // n == 0
				staleRetries++
				time.Sleep(100 * time.Millisecond)
				if staleRetries >= 5 {
					fyne.Do(func() {
						if connectionOverlay != nil {
							connectionOverlay.Show()
							connectionOverlay.Refresh()
						}
					})
					log.Printf("Re-connecting after %d retries", staleRetries)
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
					switch jsonBuffer[i] {
					case '{':
						openBrackets++
					case '}':
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

				if strings.Contains(sensor.subKeyName, "Temp") {
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
		log.Printf("Non-telemetry JSON received: %s", jsonStr)
		// Try RGB
		if strings.Contains(jsonStr, `"LED_`) {
			var rgbPayload map[string]LedSettings
			if err := json.Unmarshal([]byte(jsonStr), &rgbPayload); err == nil && len(rgbPayload) > 0 {
				isRgb := false
				for k := range rgbPayload {
					if strings.HasPrefix(k, "LED_") {
						isRgb = true
						break
					}
				}
				if isRgb {
					log.Printf("Received RGB settings: %d items", len(rgbPayload))
					rgbMutex.Lock()
					if rgbSettings == nil {
						rgbSettings = make(map[string]LedSettings)
					}
					for k, v := range rgbPayload {
						rgbSettings[k] = v
					}
					rgbMutex.Unlock()
					fyne.Do(func() {
						UpdateRgbUI()
					})
					return
				}
			}
		}

		// Try Curves (FanConfig map)
		if strings.Contains(jsonStr, `"FAN_`) {
			var fanPayload map[string]FanConfig
			if err := json.Unmarshal([]byte(jsonStr), &fanPayload); err == nil && len(fanPayload) > 0 {
				isFan := false
				for k := range fanPayload {
					if strings.HasPrefix(k, "FAN_") {
						isFan = true
						break
					}
				}
				if isFan {
					log.Printf("Received Curves settings: %d fans", len(fanPayload))
					fanConfigsMutex.Lock()
					if fanConfigs == nil {
						fanConfigs = make(map[string]FanConfig)
					}
					for k, v := range fanPayload {
						fanConfigs[k] = v
					}
					fanConfigsMutex.Unlock()
					fyne.Do(func() {
						UpdateCurvesUI()
					})
					return
				}
			}
		}

		if strings.Contains(jsonStr, `"ssid"`) {
			// Try Settings
			var settings Settings
			if err := json.Unmarshal([]byte(jsonStr), &settings); err == nil {
				log.Printf("Received Settings for %s", settings.Hostname)
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

		// Try Sensors list
		if strings.Contains(jsonStr, `"sensors"`) {
			var sensors SensorsResponse
			if err := json.Unmarshal([]byte(jsonStr), &sensors); err == nil && len(sensors.Sensors) > 0 {
				log.Printf("Received Sensors list: %v", sensors.Sensors)
				availableSensorsMutex.Lock()
				availableSensors = sensors.Sensors
				availableSensorsMutex.Unlock()

				fyne.Do(func() {
					for _, w := range fanWidgets {
						if w.TempSourceSelect != nil {
							w.TempSourceSelect.Options = sensors.Sensors
							w.TempSourceSelect.Refresh()
						}
					}
				})
			}
		}
	}
}
