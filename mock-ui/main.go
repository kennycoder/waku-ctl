package main

import (
	"fmt"
	"log"
	"math"
	"net/http"
	"time"

	"golang.org/x/exp/rand"
)

func main() {
	fmt.Println("Starting server on port 8081...")
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
	http.HandleFunc("/styles.css", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		http.ServeFile(w, r, "../data/styles.css")
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

	http.HandleFunc("/get-sensors", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		fmt.Fprintf(w, `["TEMP_1", "TEMP_2", "TEMP_3"]`)
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
		fmt.Fprintf(w, `{"FAN_0": {"mode": 1, "units": "C", "curves": [{"temp": 30, "fan": 20}, {"temp": 33, "fan": 40}, {"temp": 36, "fan": 60}, {"temp": 39, "fan": 80}, {"temp": 42, "fan": 100}], "sensor": "TEMP_1"}, 
			"FAN_1": {"mode": 0, "units": "C", "curves": [{"temp": 30, "fan": 20}, {"temp": 33, "fan": 40}, {"temp": 36, "fan": 60}, {"temp": 39, "fan": 80}, {"temp": 42, "fan": 100}], "sensor": "TEMP_1"}, 
			"FAN_2": {"mode": 1, "units": "C", "curves": [{"temp": 30, "fan": 20}, {"temp": 33, "fan": 40}, {"temp": 36, "fan": 60}, {"temp": 39, "fan": 80}, {"temp": 42, "fan": 100}], "sensor": "TEMP_1"}, 
			"FAN_3": {"mode": 0, "units": "C", "curves": [{"temp": 30, "fan": 20}, {"temp": 33, "fan": 40}, {"temp": 36, "fan": 60}, {"temp": 39, "fan": 80}, {"temp": 42, "fan": 100}], "sensor": "TEMP_1"}}`)
	})

	http.HandleFunc("/get-rgb", func(w http.ResponseWriter, r *http.Request) {
		h1 := fmt.Sprintf("%x", 1235623)
		h2 := fmt.Sprintf("%x", 2315235)
		h3 := fmt.Sprintf("%x", 7134613)
		h4 := fmt.Sprintf("%x", 5847134)

		fmt.Fprintf(w, `{"LED_0": {"mode": 2, "speed": 69, "start_color": "%s", "end_color": "%s", "num_leds": 16}, "LED_1": {"mode": 1, "speed": 69, "start_color": "%s", "end_color": "%s", "num_leds": 59}}`, h1, h2, h3, h4)
	})

	http.HandleFunc("/get-data", func(w http.ResponseWriter, r *http.Request) {
		noCacheHeaders(w)
		rnd := rand.New(rand.NewSource(uint64(time.Now().UnixNano())))
		min := 30
		max := 36
		fmin := 1030
		fmax := 1630

		v1 := (rnd.Intn(max-min+1) + min)
		v2 := (rnd.Intn(max-min+1) + min)
		v3 := (rnd.Intn(max-min+1) + min)
		v4 := math.Abs(float64(v1 - v2))
		v5 := math.Abs(float64(v1 - v3))
		v6 := math.Abs(float64(v2 - v3))
		v7 := (rnd.Intn(fmax-fmin+1) + fmin)
		v8 := (rnd.Intn(fmax-fmin+1) + fmin)
		v9 := (rnd.Intn(fmax-fmin+1) + fmin)
		v10 := (rnd.Intn(fmax-fmin+1) + fmin)

		fmt.Fprintf(w, `{"client_id":"14:38:EB:AE:3D:98","event":"manual_fetch","units":"C","data":{"temps":{"TEMP_1":%d,"TEMP_2":%d,"TEMP_3":%d,"DELTA_T1_T2":%.2f,"DELTA_T1_T3":%.2f,"DELTA_T2_T3":%.2f},"fans":{"FAN_PUMP":%d,"FAN_1":%d,"FAN_2":%d,"FAN_3":%d}}}`, v1, v2, v3, v4, v5, v6, v7, v8, v9, v10)
	})

	log.Fatal(http.ListenAndServe("127.0.0.1:8081", nil))
}

func noCacheHeaders(w http.ResponseWriter) {
	w.Header().Set("Cache-control", "no-cache, no-store, must-revalidate, max-age=0")
	w.Header().Set("Pragma", "no-cache")
	w.Header().Set("Expires", "0")
}
