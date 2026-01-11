//go:build !windows
// +build !windows

package main

func updateRegistry(telemetry TelemetryData) {
	// No-op for non-Windows platforms
}
