# IoT Equipment Monitoring System

Distributed real-time monitoring platform for industrial IoT equipment, deployed on AWS with Docker.

---

## Overview

This system monitors simulated industrial equipment through a central server that collects sensor data, detects anomalies, and broadcasts metrics and alerts to connected operators.

**Components:**

- **Server (C)** — Multithreaded TCP server using Berkeley Sockets and pthreads. Manages client sessions, processes commands, broadcasts metrics, evaluates alerts, serves a web dashboard, and authenticates users via an external identity service.
- **Sensor client (Go)** — Simulates 6 IoT sensors running as concurrent goroutines with automatic reconnection.
- **Operator client (Python)** — GUI dashboard with real-time metrics, alert panel, equipment controls, and command log.
- **Web interface (HTTP)** — Browser-accessible dashboard with login, system status, sensor list, alerts, and JSON API.

---

## Architecture

```
                    ┌──────────────────────────┐
                    │   Server (C)             │
                    │   TCP :9000 + HTTP :9080 │
                    │                          │
                    │   8 modules:             │
                    │   equipment_state        │
                    │   connection_handler     │
                    │   metrics_processor      │
                    │   alert_system           │
                    │   auth_service           │
                    │   http_handler           │
                    │   logger                 │
                    └─────────┬────────────────┘
                              │
                 ┌────────────┴────────────┐
                 │                         │
      ┌──────────┴───────┐     ┌───────────┴──────────┐
      │ Sensor Client    │     │ Operator Client      │
      │ (Go)             │     │ (Python)             │
      │ 6 IoT sensors    │     │ Real-time dashboard  │
      └──────────────────┘     └──────────────────────┘
                 │                         │
                 └────── Web Browser ──────┘
                      HTTP :9080
                      Login + Dashboard
```

---

## Requirements

| Component | Technology | Version |
|-----------|-----------|---------|
| Server | C (GCC) | C11 |
| Threading | POSIX pthreads | — |
| Transport | TCP/IP | IPv4 |
| Sensor client | Go | 1.21+ |
| Operator client | Python + customtkinter | 3.10+ |
| Container | Docker + Docker Compose | 20+ |
| Web | Embedded HTTP/1.1 server | — |

---

## Quick start

### 1. Compile and run the server

```bash
cd server
make clean && make
./server 9000 server.log
```

The server starts two services:
- **TCP protocol** on port 9000
- **HTTP web dashboard** on port 9080

### 2. Run the sensor client (Go)

```bash
cd clients/sensor_client
go run . localhost 9000
```

### 3. Run the operator client (Python)

```bash
cd clients/operator_client
pip install -r requirements.txt
python3 main.py localhost 9000
```

A login window will appear. Enter your credentials from `users.conf` to access the dashboard.

### 4. Open the web dashboard

Open `http://localhost:9080` in your browser.
Login with any user from `users.conf` (e.g. `engineer` / `eng2026`)

---

## Web interface

| URL | Description |
|-----|-------------|
| `http://localhost:9080` | Login page |
| `http://localhost:9080/dashboard` | Real-time dashboard (auto-refresh 5s) |
| `http://localhost:9080/api/status` | JSON equipment status |
| `http://localhost:9080/api/alerts` | JSON recent alerts |
| `http://localhost:9080/api/sensors` | JSON sensor fleet |

The HTTP server implements correct headers, GET method handling, and status codes (200, 302, 400, 404, 405).

---

## External authentication

Users are NOT stored in the server. Credentials are validated against an external identity file (`users.conf`):

```
engineer:eng2026:ENGINEER
supervisor:sup2026:SUPERVISOR
operator1:op123:SUPERVISOR
```

The file is re-read on every authentication attempt. In Docker, mount it as a volume for external management.

---

## Docker deployment

```bash
docker compose up -d
docker compose logs -f
```

Connect clients using `localhost` (local) or `<PUBLIC_IP>` (AWS).

---

## AWS deployment

1. Launch Ubuntu 22.04 EC2 (t2.micro)
2. Open ports: 22 (SSH), 9000 (TCP protocol), 9080 (HTTP dashboard)
3. Install Docker, clone repo, `docker compose up -d`

See [docs/DEPLOYMENT.md](docs/DEPLOYMENT.md) for step-by-step instructions.

---

## Protocol summary

| Command | Direction | Description |
|---------|-----------|-------------|
| `HELLO [name=<text>]` | Client → Server | Identify client |
| `AUTHENTICATE <user> <pass>` | Client → Server | Auth via external service |
| `GET_STATUS` | Client → Server | Equipment snapshot |
| `GET_ALERTS` | Client → Server | Alert history |
| `MODIFY_RPM <delta>` | Client → Server | Change RPM (ENGINEER) |
| `ADJUST_HEADING <LEFT\|RIGHT>` | Client → Server | Rotate heading (ENGINEER) |
| `SENSOR_DATA <id>\|<type>\|<val>\|<unit>\|<ts>` | Client → Server | Sensor reading |
| `QUIT` | Client → Server | Close session |
| `METRIC rpm=..\|load=..\|...` | Server → Client | Periodic broadcast (5s) |
| `ALERT <severity>\|<type>\|<msg>` | Server → Client | Anomaly notification |

See [docs/PROTOCOL.md](docs/PROTOCOL.md) for the full specification.

---

## Simulated sensors

| Equipment ID | Type | Unit | Range |
|-------------|------|------|-------|
| PUMP-001 | Pressure | bar | 30–70 |
| MOTOR-001 | RPM | rev/min | 1500–3500 |
| COOLER-001 | Temperature | °C | 35–55 |
| VIBR-001 | Vibration | mm/s | 1–5 |
| ENERGY-001 | Energy | kW | 25–50 |
| HUMIDITY-001 | Humidity | % | 40–70 |

---

## Alert system

| Type | Warning | Critical |
|------|---------|----------|
| rpm_high | > 4000 | > 4500 |
| load_low | < 20% | < 10% |
| temp_high | > 70°C | > 85°C |
| pressure_high | > 80 bar | > 90 bar |
| pressure_low | < 15 bar | < 8 bar |

---

## Project structure

```
PROYECTO_1_TELEMATICA/
├── server/
│   ├── server.c                 Main orchestrator
│   ├── equipment_state.h/c      Thread-safe equipment state
│   ├── connection_handler.h/c   Client sessions + commands
│   ├── metrics_processor.h/c    Periodic broadcast + alerts
│   ├── alert_system.h/c         Anomaly detection + history
│   ├── auth_service.h/c         External identity service
│   ├── http_handler.h/c         Embedded HTTP server + web UI
│   ├── logger.h/c               Centralised logging
│   ├── Makefile                 Build system
│   ├── Dockerfile               Multi-stage Docker build
│   └── users.conf               External credentials (mounted as Docker volume)
├── clients/
│   ├── sensor_client/           Go — 6 IoT sensors
│   │   ├── main.go              Entry point
│   │   ├── sensor.go            Sensor simulation
│   │   ├── protocol.go          TCP + DNS + protocol
│   │   ├── reconnect.go         Auto-reconnect
│   │   └── go.mod               Module definition
│   └── operator_client/         Python — operator dashboard
│       ├── main.py              Entry point
│       ├── dashboard.py         GUI (customtkinter)
│       ├── network.py           TCP + DNS + protocol
│       ├── alerts.py            Alert management
│       └── requirements.txt     Dependencies
├── docs/
│   ├── PROTOCOL.md              Protocol specification
│   ├── ARCHITECTURE.md          System design
│   └── DEPLOYMENT.md            Deployment guide
├── docker-compose.yml           Service orchestration
├── README.md                    This file
└── .gitignore                   Git ignore rules
```

---

## Authors

- Jose Restrepo Tamayo
- Moises Vergara Garces
- Hever Andre Alfonso Jimenez
- Sebastian Ramirez Lopez

## Course

Internet: Arquitectura y Protocolos — Universidad EAFIT — 2026