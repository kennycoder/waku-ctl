//go:build windows

package main

import (
	"log"

	"golang.org/x/sys/windows/registry"
)

const RegistryKey = `Software\HWiNFO64\Sensors\Custom\WaKu Controller`

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
