package main

import (
	"fmt"
	"log"
	"net/http"
	"time"

	"golang.org/x/exp/rand"
)

func main() {
	fmt.Println("Starting server...")
	http.HandleFunc("/", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/index.html")
	})
	http.HandleFunc("/fans", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/fans.html")
	})
	http.HandleFunc("/setup", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/setup.html")
	})
	http.HandleFunc("/settings", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/settings.html")
	})
	http.HandleFunc("/rgb", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/rgb.html")
	})
	http.HandleFunc("/curves", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/curves.html")
	})
	http.HandleFunc("/all-jquery-deps.min.js", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/all-jquery-deps.min.js")
	})
	http.HandleFunc("/logo.png", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/logo.png")
	})

	// Mocked json

	http.HandleFunc("/save-settings", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, `{"status": "settings_saved"}`)
	})
	http.HandleFunc("/save-curves", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, `{"status": "curves_saved"}`)
	})
	http.HandleFunc("/save-rgb", func(w http.ResponseWriter, r *http.Request) {
		fmt.Fprintf(w, `{"status": "rgb_saved"}`)
	})

	http.HandleFunc("/networks", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		fmt.Fprintf(w, `{"networks": ["wifi 1", "wifi 2", "wifi 3"]}`)
	})

	http.HandleFunc("/get-settings", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		fmt.Fprintf(w, `
			{
			"ssid":"wifi 1",
			"password":"wifi password",
			"hostname":"waku-ctl.local",
			"offline_mode":false,
			"setup_done":false,
			"tel_itv":60,
			"mqtt_enable":false,
			"mqtt_broker":"broker.emqx.io",
			"mqtt_username":"username",
			"mqtt_password":"password",
			"mqtt_topic":"waku-ctl/telemetry/AA:BB:CC:DD:EE",
			"mqtt_port":1883
			}		
		`)
	})

	http.HandleFunc("/get-curves", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		fmt.Fprintf(w, `{"FAN_0": {"units": "C", "curves": [{"temp": 30, "fan": 20}, {"temp": 33, "fan": 40}, {"temp": 36, "fan": 60}, {"temp": 39, "fan": 80}, {"temp": 42, "fan": 100}], "sensor": "TEMP_1"}, "FAN_1": {"units": "C", "curves": [], "sensor": "TEMP_1"}, "FAN_2": {"units": "C", "curves": [], "sensor": "TEMP_1"}, "FAN_3": {"units": "C", "curves": [], "sensor": "TEMP_1"}}`)
	})

	http.HandleFunc("/get-rgb", func(w http.ResponseWriter, r *http.Request) {
		h1 := fmt.Sprintf("%x", 1235623)
		h2 := fmt.Sprintf("%x", 2315235)
		h3 := fmt.Sprintf("%x", 7134613)
		h4 := fmt.Sprintf("%x", 5847134)

		fmt.Fprintf(w, `{"LED_1": {"mode": 2, "speed": 69, "start_color": "#%s", "end_color": "#%s", "num_leds": 16}, "LED_2": {"mode": 1, "speed": 69, "start_color": "#%s", "end_color": "#%s", "num_leds": 59}}`, h1, h2, h3, h4)
	})

	http.HandleFunc("/get-data", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		rnd := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
		min := 10
		max := 30

		fmt.Fprintf(w, `{"units": "C", "client_id": "00:1b:63:84:45:e6", "local_timestamp": 946684800, "event": "manual_telemetry", "data": {"temperature1": %d, "temperature2": %d, "FAN_0": 128,"FAN_1": 128,"FAN_2": 128,"FAN_3": 128}}`, (rnd.Intn(max-min+1) + min), (rnd.Intn(max-min+1) + min))
	})

	log.Fatal(http.ListenAndServe("127.0.0.1:8081", nil))
}

func noCacheHeaders(w http.ResponseWriter) {
	w.Header().Set("Cache-control", "no-cache, no-store, must-revalidate, max-age=0")
	w.Header().Set("Pragma", "no-cache")
	w.Header().Set("Expires", "0")
}
