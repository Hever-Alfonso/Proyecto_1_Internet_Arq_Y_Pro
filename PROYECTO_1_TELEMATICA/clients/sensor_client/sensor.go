/*
 * sensor.go
 * ---------
 * Definition and simulation of 6 IoT sensor devices.
 *
 * Each sensor has:
 *   - A unique equipment ID  (e.g. "PUMP-001")
 *   - A sensor type          (e.g. "pressure")
 *   - A base value around which readings fluctuate
 *   - A drift factor that slowly shifts the base over time
 *   - Random noise on every reading
 *
 * Sensor catalogue:
 *   1. PUMP-001      — hydraulic pressure      (bar)
 *   2. MOTOR-001     — RPM                     (rev/min)
 *   3. COOLER-001    — coolant temperature      (°C)
 *   4. VIBR-001      — vibration amplitude      (mm/s)
 *   5. ENERGY-001    — power consumption        (kW)
 *   6. HUMIDITY-001  — ambient humidity          (%)
 */

package main

import (
	"fmt"
	"math"
	"math/rand"
	"sync"
	"time"
)

// SensorType identifies what physical quantity the sensor measures.
type SensorType string

const (
	SensorPressure    SensorType = "pressure"
	SensorRPM         SensorType = "rpm"
	SensorTemperature SensorType = "temperature"
	SensorVibration   SensorType = "vibration"
	SensorEnergy      SensorType = "energy"
	SensorHumidity    SensorType = "humidity"
)

// SensorReading is a single data point produced by a sensor.
type SensorReading struct {
	EquipmentID string
	SensorType  SensorType
	Value       float64
	Unit        string
	Timestamp   time.Time
}

// Sensor represents a simulated IoT sensor device.
type Sensor struct {
	EquipmentID string
	Type        SensorType
	Unit        string

	baseValue   float64 // centre of the reading range
	noiseRange  float64 // max random deviation per tick
	driftRate   float64 // slow shift per tick (can be negative)
	minValue    float64
	maxValue    float64

	mu          sync.Mutex
	currentBase float64
	rng         *rand.Rand
}

// NewSensor creates a sensor with the given parameters.
func NewSensor(id string, stype SensorType, unit string,
	base, noise, drift, min, max float64) *Sensor {

	return &Sensor{
		EquipmentID: id,
		Type:        stype,
		Unit:        unit,
		baseValue:   base,
		noiseRange:  noise,
		driftRate:   drift,
		minValue:    min,
		maxValue:    max,
		currentBase: base,
		rng:         rand.New(rand.NewSource(time.Now().UnixNano() + int64(rand.Intn(10000)))),
	}
}

// Read produces a single simulated reading.
func (s *Sensor) Read() SensorReading {
	s.mu.Lock()
	defer s.mu.Unlock()

	// Apply drift to base (reverses direction at boundaries)
	s.currentBase += s.driftRate
	if s.currentBase > s.maxValue*0.9 || s.currentBase < s.minValue+s.baseValue*0.1 {
		s.driftRate = -s.driftRate
	}

	// Generate reading = base + noise
	noise := (s.rng.Float64()*2 - 1) * s.noiseRange
	value := s.currentBase + noise

	// Clamp to valid range
	value = math.Max(s.minValue, math.Min(s.maxValue, value))

	// Round to 1 decimal
	value = math.Round(value*10) / 10

	return SensorReading{
		EquipmentID: s.EquipmentID,
		SensorType:  s.Type,
		Value:       value,
		Unit:        s.Unit,
		Timestamp:   time.Now(),
	}
}

// FormatMetric returns the reading as a protocol-ready string:
//
//	SENSOR_DATA PUMP-001|pressure|48.5|bar|2025-03-30 14:30:00
func (r *SensorReading) FormatMetric() string {
	ts := r.Timestamp.Format("2006-01-02 15:04:05")
	return fmt.Sprintf("SENSOR_DATA %s|%s|%.1f|%s|%s",
		r.EquipmentID, r.SensorType, r.Value, r.Unit, ts)
}

// ── Factory: create the default fleet of 6 sensors ──

func CreateDefaultSensors() []*Sensor {
	return []*Sensor{
		// 1. Hydraulic pump — pressure sensor
		NewSensor("PUMP-001", SensorPressure, "bar",
			48.0,  // base: 48 bar
			5.0,   // noise: ±5 bar
			0.15,  // drift: slow climb
			0.0,   // min
			100.0, // max
		),

		// 2. Main motor — RPM sensor
		NewSensor("MOTOR-001", SensorRPM, "rev/min",
			2200.0, // base: 2200 RPM
			150.0,  // noise: ±150 RPM
			5.0,    // drift
			0.0,
			5000.0,
		),

		// 3. Cooling unit — temperature sensor
		NewSensor("COOLER-001", SensorTemperature, "C",
			42.0, // base: 42°C
			3.0,  // noise: ±3°C
			0.1,  // drift: slow warming
			0.0,
			100.0,
		),

		// 4. Vibration monitor
		NewSensor("VIBR-001", SensorVibration, "mm/s",
			2.5,  // base: 2.5 mm/s
			1.2,  // noise: ±1.2 mm/s
			0.05, // drift
			0.0,
			20.0,
		),

		// 5. Power consumption monitor
		NewSensor("ENERGY-001", SensorEnergy, "kW",
			35.0, // base: 35 kW
			4.0,  // noise: ±4 kW
			0.2,  // drift
			0.0,
			100.0,
		),

		// 6. Ambient humidity sensor
		NewSensor("HUMIDITY-001", SensorHumidity, "percent",
			55.0, // base: 55%
			5.0,  // noise: ±5%
			0.1,  // drift
			0.0,
			100.0,
		),
	}
}