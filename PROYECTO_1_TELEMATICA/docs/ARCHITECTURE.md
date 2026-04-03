# System Architecture

## General overview

```
┌──────────────────────────────────────────────────────────────┐
│                    AWS EC2 + Docker                          │
│  ┌────────────────────────────────────────────────────────┐  │
│  │           IoT Monitoring Server (C)                    │  │
│  │                                                        │  │
│  │  ┌──────────────┐  ┌───────────────┐  ┌────────────┐  │  │
│  │  │ equipment    │  │ connection    │  │ metrics    │  │  │
│  │  │ _state       │  │ _handler     │  │ _processor │  │  │
│  │  └──────────────┘  └───────────────┘  └────────────┘  │  │
│  │  ┌──────────────┐  ┌───────────────┐  ┌────────────┐  │  │
│  │  │ alert        │  │ auth          │  │ http       │  │  │
│  │  │ _system      │  │ _service      │  │ _handler   │  │  │
│  │  └──────────────┘  └───────────────┘  └────────────┘  │  │
│  │  ┌──────────────┐  ┌───────────────┐                  │  │
│  │  │ logger       │  │ server.c      │                  │  │
│  │  │              │  │ (main)        │                  │  │
│  │  └──────────────┘  └───────────────┘                  │  │
│  │                                                        │  │
│  │  TCP :9000 (protocol)    HTTP :9080 (web dashboard)   │  │
│  └────────────────────────────────────────────────────────┘  │
└──────────────────────────────────────────────────────────────┘
          ↕ TCP/IP                    ↕ TCP/IP
          │                           │
    ┌─────┴──────────┐         ┌──────┴───────────────┐
    │ Sensor Client  │         │ Operator Client      │
    │ (Go)           │         │ (Python)             │
    │                │         │                      │
    │ 6 sensors:     │         │ Dashboard GUI:       │
    │  PUMP-001      │         │  - Real-time metrics │
    │  MOTOR-001     │         │  - Alert panel       │
    │  COOLER-001    │         │  - Equipment control  │
    │  VIBR-001      │         │  - Command log       │
    │  ENERGY-001    │         │                      │
    │  HUMIDITY-001  │         │ Role: ENGINEER       │
    │                │         │ (full control)       │
    │ Auto-reconnect │         │                      │
    │ DNS resolution │         │ DNS resolution       │
    └────────────────┘         └──────────────────────┘
          │                           │
          └───── Web Browser ─────────┘
                     │
              ┌──────┴──────────┐
              │ HTTP Dashboard  │
              │ :9080           │
              │ Login + Status  │
              │ Sensors + Alerts│
              │ JSON API        │
              └─────────────────┘
```

---

## Server modules

### server.c (main)

Orchestrator — wires all modules together.

- Parses CLI arguments (port, logfile)
- Initialises all modules (equipment, alerts, auth, logger)
- Creates TCP listening socket (protocol port)
- Starts metrics broadcast thread
- Starts HTTP server thread (web dashboard)
- Runs accept loop (1 thread per client)
- Handles SIGINT/SIGTERM for graceful shutdown

### equipment_state (.h/.c)

Thread-safe management of the equipment's physical state.

- Holds RPM, system load, coolant temperature, hydraulic pressure, heading
- Single mutex protects all access
- `equipment_state_read()` for consistent snapshots
- `equipment_modify_rpm()` and `equipment_adjust_heading()` for mutations
- `equipment_simulate_tick()` models wear each broadcast cycle

**Dependencies:** none (leaf module).

### connection_handler (.h/.c)

Client registry and session management.

- Linked list of connected clients (mutex-protected)
- One detached thread per client
- Parses and dispatches all protocol commands including SENSOR_DATA
- `connection_broadcast()` for sending data to all clients
- Authentication via external auth_service (no local credentials)

**Dependencies:** equipment_state, alert_system, auth_service, logger.

### metrics_processor (.h/.c)

Periodic metrics capture, alert evaluation, and broadcast.

- Own thread (started by server.c)
- Each cycle: simulate → read → format METRIC → evaluate alerts → broadcast
- Broadcast via injected callback (decoupled from client list)
- Configurable interval (default 5 seconds)

**Dependencies:** equipment_state, alert_system, logger.

### alert_system (.h/.c)

Anomaly detection and alert history.

- Evaluates snapshots against warning/critical thresholds
- 5 conditions: rpm_high, load_low, temp_high, pressure_high, pressure_low
- Ring buffer of 64 entries for history
- Formats ALERT protocol lines

**Dependencies:** equipment_state (types only).

### auth_service (.h/.c)

External identity service client.

- Reads credentials from external file (`users.conf`)
- File is re-read on every auth attempt (no local caching)
- Supports ENGINEER and SUPERVISOR roles
- Creates default credentials file on first run
- File can be mounted as Docker volume for external management

**Dependencies:** logger.

### http_handler (.h/.c)

Embedded HTTP server for web dashboard.

- Runs on protocol_port + 80 (e.g. 9080)
- Own accept loop in dedicated thread
- Handles GET requests with correct HTTP/1.1 headers
- Status codes: 200, 302, 400, 404, 405
- Cookie-based authentication
- Serves: login page, dashboard (auto-refresh 5s), JSON API
- JSON endpoints: /api/status, /api/alerts, /api/sensors

**Dependencies:** equipment_state, alert_system, connection_handler, logger.

### logger (.h/.c)

Centralised logging to stderr and file.

- Thread-safe (single mutex)
- Timestamps every entry
- Append mode with fflush after each write
- `logger_peer_id()` formats sockaddr_in to "ip:port"

**Dependencies:** none (leaf module).

---

## Module dependency graph

```
server.c
  ├── equipment_state    (leaf)
  ├── logger             (leaf)
  ├── auth_service       ← logger
  ├── alert_system       ← equipment_state
  ├── metrics_processor  ← equipment_state, alert_system, logger
  ├── connection_handler ← equipment_state, alert_system, auth_service, logger
  └── http_handler       ← equipment_state, alert_system, connection_handler, logger
```

No circular dependencies.

---

## Threading model

```
Thread 0 — Main
  │  accept() loop → spawn client threads
  │
Thread 1 — Metrics broadcast
  │  every 5s: simulate → capture → broadcast METRIC → evaluate alerts
  │
Thread 2 — HTTP server accept loop
  │  accept() → spawn short-lived HTTP threads
  │
Thread 3..N — Client sessions (1 per TCP client, detached)
  │  recv() loop → parse command → dispatch → respond
  │
Thread N+1..M — HTTP request handlers (short-lived, detached)
     recv() → parse HTTP → route → respond → close
```

### Synchronisation

| Mutex | Protects | Used by |
|-------|----------|---------|
| equipment_state lock | RPM, load, temp, pressure, heading | equipment_state, metrics_processor, connection_handler, http_handler |
| registry lock | Client linked list | connection_handler (add/remove/broadcast/list), http_handler (count) |
| logger lock | stderr + log file | logger (all writes) |
| alert history lock | Alert ring buffer | alert_system (push/copy), http_handler (copy) |

All locks are fine-grained and never nested — no deadlock risk.

---

## Data flow

### Metric broadcast cycle

```
metrics_processor thread
  ├─ 1. equipment_simulate_tick()
  ├─ 2. equipment_state_read(&snapshot)
  ├─ 3. format METRIC line → connection_broadcast()
  ├─ 4. alert_evaluate(&snapshot) → push to ring buffer
  ├─ 5. format ALERT lines → connection_broadcast()
  └─ 6. sleep 5s (interruptible)
```

### Client command flow

```
client session thread
  ├─ recv() → split on \n → dispatch_command()
  │    ├─ HELLO           → update name
  │    ├─ AUTHENTICATE    → auth_service_validate() (external)
  │    ├─ GET_STATUS      → equipment_state_read()
  │    ├─ MODIFY_RPM      → role check → equipment_modify_rpm()
  │    ├─ ADJUST_HEADING  → role check → equipment_adjust_heading()
  │    ├─ GET_ALERTS      → alert_history_copy()
  │    ├─ LIST USERS      → role check → iterate registry
  │    ├─ SENSOR_DATA     → log sensor reading
  │    └─ QUIT            → respond BYE, exit
  └─ on disconnect: connection_registry_remove(fd)
```

### HTTP request flow

```
http_client_thread
  ├─ recv() HTTP request
  ├─ parse method + path
  ├─ route:
  │    ├─ GET /           → login page HTML
  │    ├─ GET /login?...  → validate → set cookie → redirect
  │    ├─ GET /dashboard  → check cookie → generate dashboard HTML
  │    ├─ GET /api/status → equipment_state_read() → JSON
  │    ├─ GET /api/alerts → alert_history_copy() → JSON
  │    ├─ GET /api/sensors→ connection_registry_count() → JSON
  │    └─ other           → 404 HTML
  └─ close connection
```

---

## Sensor client architecture (Go)

```
main.go
  ├─ CreateDefaultSensors() → 6 Sensor structs
  ├─ NewReconnectManager()  → connection lifecycle
  │    ├─ ServerConnection  → TCP + DNS resolution + handshake
  │    └─ exponential backoff → 2s, 4s, 8s, ..., 30s cap
  └─ per sensor goroutine
       └─ every 5s: sensor.Read() → conn.SendSensorData()
```

Concurrency: 1 goroutine per sensor + 1 receiver + 1 reconnect manager.
DNS: `net.DialTimeout()` resolves hostnames automatically.

---

## Operator client architecture (Python)

```
main.py
  ├─ NetworkManager        → TCP + DNS (getaddrinfo) + handshake
  │    ├─ receiver thread  → parses METRIC, ALERT, STATUS
  │    └─ callbacks        → on_metric, on_alert, on_log
  ├─ AlertManager          → alert history, stats, filtering
  └─ OperatorDashboard     → customtkinter GUI
       ├─ Metrics panel    → RPM, load, temp, pressure, heading
       ├─ Control panel    → RPM ±100/±500, heading, queries
       ├─ Alerts panel     → list, count, acknowledge
       └─ Command log      → timestamped communication log
```

DNS: uses `socket.getaddrinfo()` for explicit name resolution.

---

## Design decisions

| Decision | Rationale |
|----------|-----------|
| C for server | Course requirement; Berkeley sockets + pthreads |
| Modular .h/.c files | Separation of concerns, testability |
| Go for sensor client | Goroutines ideal for concurrent sensors; single binary |
| Python for operator | Rapid GUI development; different language from Go |
| Text protocol with `\|` separator | Human-readable, debuggable with netcat |
| Callback-based broadcast | Decouples metrics_processor from connection_handler |
| External auth file | PDF requirement: no local user storage |
| Ring buffer for alerts | Bounded memory, O(1) push |
| Embedded HTTP server | PDF requirement: web interface for login/status/sensors |
| Cookie-based HTTP auth | Simple session management without external libs |
| DNS resolution (getaddrinfo) | PDF requirement: no hardcoded IPs |
| Multi-stage Docker | Smaller runtime image |
| Exponential backoff | Prevents reconnection storms |

---

## Security considerations

- External credential storage (not compiled into binary)
- Role-based access control (SUPERVISOR vs ENGINEER)
- Cookie-based HTTP authentication
- Input validation on all commands
- Non-root Docker user
- MSG_NOSIGNAL on send() to prevent SIGPIPE
- Graceful shutdown on SIGINT with resource cleanup
- DNS resolution failure handling without crash