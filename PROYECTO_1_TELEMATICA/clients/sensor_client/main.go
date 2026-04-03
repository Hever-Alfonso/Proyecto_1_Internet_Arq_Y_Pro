/*
 * main.go
 * -------
 * Entry point for the IoT Sensor Client (Go).
 *
 * Usage:
 *   go run . <host> <port>
 *   go run . 127.0.0.1 9000
 *
 * What it does:
 *   1. Creates 6 simulated sensors (pressure, rpm, temperature,
 *      vibration, energy, humidity)
 *   2. Connects to the monitoring server with auto-reconnection
 *   3. Launches one goroutine per sensor
 *   4. Each goroutine reads its sensor every 5 seconds and sends
 *      the data to the server
 *   5. Handles SIGINT for graceful shutdown
 *
 * This client uses ONLY Go standard library — no external deps.
 */

package main

import (
	"fmt"
	"os"
	"os/signal"
	"sync"
	"syscall"
	"time"
)

const (
	defaultHost     = "127.0.0.1"
	defaultPort     = "9000"
	clientName      = "sensor-fleet-go"
	authUser        = "engineer"
	authPassword    = "eng2025"
	publishInterval = 5 * time.Second
)

func main() {
	// ── Parse arguments ──
	host := defaultHost
	port := defaultPort

	if len(os.Args) >= 3 {
		host = os.Args[1]
		port = os.Args[2]
	} else if len(os.Args) == 2 {
		host = os.Args[1]
	}

	fmt.Println("═══════════════════════════════════════════")
	fmt.Println("  IoT Sensor Client (Go)")
	fmt.Println("═══════════════════════════════════════════")
	fmt.Printf("  Server:   %s:%s\n", host, port)
	fmt.Printf("  Sensors:  6 devices\n")
	fmt.Printf("  Interval: %v\n", publishInterval)
	fmt.Println("═══════════════════════════════════════════")

	// ── Create sensors ──
	sensors := CreateDefaultSensors()
	fmt.Printf("\nRegistered %d sensors:\n", len(sensors))
	for i, s := range sensors {
		fmt.Printf("  %d. %-15s  type=%-12s  unit=%s\n",
			i+1, s.EquipmentID, s.Type, s.Unit)
	}
	fmt.Println()

	// ── Setup reconnection manager ──
	rm := NewReconnectManager(host, port, clientName, authUser, authPassword,
		DefaultReconnectConfig())

	rm.OnStatus = func(status ConnectionStatus) {
		fmt.Printf("[%s] STATUS: %s\n",
			time.Now().Format("15:04:05"), status)
	}

	rm.OnMessage = func(line string) {
		// Server messages (METRIC, ALERT) are already logged by protocol.go
		// Additional processing can go here if needed
	}

	rm.Start()

	// ── Wait for connection before starting sensors ──
	fmt.Println("Waiting for connection...")
	for i := 0; i < 30; i++ {
		conn := rm.GetConnection()
		if conn != nil && conn.IsConnected() {
			break
		}
		time.Sleep(1 * time.Second)
	}

	// ── Launch sensor goroutines ──
	var wg sync.WaitGroup
	stopCh := make(chan struct{})

	for _, sensor := range sensors {
		wg.Add(1)
		go func(s *Sensor) {
			defer wg.Done()
			runSensor(s, rm, stopCh)
		}(sensor)
	}

	fmt.Printf("\n[%s] All %d sensor goroutines running. Press Ctrl+C to stop.\n\n",
		time.Now().Format("15:04:05"), len(sensors))

	// ── Wait for SIGINT ──
	sigCh := make(chan os.Signal, 1)
	signal.Notify(sigCh, syscall.SIGINT, syscall.SIGTERM)
	<-sigCh

	fmt.Printf("\n[%s] Shutdown requested...\n", time.Now().Format("15:04:05"))

	// ── Graceful shutdown ──
	close(stopCh)
	wg.Wait()
	rm.Stop()

	fmt.Println("Sensor client stopped.")
}

// runSensor is the goroutine body for a single sensor.
// It reads the sensor and publishes data at the configured interval.
func runSensor(sensor *Sensor, rm *ReconnectManager, stopCh <-chan struct{}) {
	// Stagger startup so sensors don't all fire at once
	stagger := time.Duration(100+len(sensor.EquipmentID)*50) * time.Millisecond
	select {
	case <-time.After(stagger):
	case <-stopCh:
		return
	}

	ticker := time.NewTicker(publishInterval)
	defer ticker.Stop()

	for {
		select {
		case <-stopCh:
			fmt.Printf("[%s] %s: goroutine stopped\n",
				time.Now().Format("15:04:05"), sensor.EquipmentID)
			return

		case <-ticker.C:
			conn := rm.GetConnection()
			if conn == nil || !conn.IsConnected() {
				fmt.Printf("[%s] %s: skipping — not connected\n",
					time.Now().Format("15:04:05"), sensor.EquipmentID)
				continue
			}

			reading := sensor.Read()
			err := conn.SendSensorData(reading)
			if err != nil {
				fmt.Printf("[%s] %s: send error: %v\n",
					time.Now().Format("15:04:05"), sensor.EquipmentID, err)
			}
		}
	}
}