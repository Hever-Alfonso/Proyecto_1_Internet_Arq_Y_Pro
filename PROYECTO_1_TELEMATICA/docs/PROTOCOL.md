# IoT Equipment Monitoring Protocol — Specification v2.1

## Overview

Text-based application protocol for communication between IoT sensor clients, operator clients, and the central monitoring server. All messages are UTF-8 encoded and terminated with `\n`.

**Transport:** TCP (SOCK_STREAM)
**Default port:** 9000 (protocol) / 9080 (HTTP web interface)
**Max message size:** 4096 bytes

---

## Roles

| Role | Access |
|------|--------|
| **SUPERVISOR** | Read-only: receives metrics, alerts, can query status |
| **ENGINEER** | Full access: all supervisor capabilities + equipment control |

Default role on connection is SUPERVISOR. Use AUTHENTICATE to upgrade.

Credentials are NOT stored in the server — they are validated against an external identity service (`users.conf`).

---

## Client → Server commands

### HELLO

Identify the client with an optional name.

```
Format:   HELLO [name=<text>]
Response: OK hello <name|supervisor>
```

### AUTHENTICATE

Request role upgrade via external identity service.

```
Format:   AUTHENTICATE <username> <password>
Response: OK authenticated
          ERR invalid_credentials
```

The server queries the external identity file (`users.conf`) on each authentication attempt. Users are never stored in server memory.

### ROLE?

Query current assigned role.

```
Format:   ROLE?
Response: OK ENGINEER
          OK SUPERVISOR
```

### GET_STATUS

Request current equipment snapshot.

```
Format:   GET_STATUS
Response: STATUS rpm=<int>|load=<int>|temp=<int>|pressure=<int>|heading=<char>
```

### GET_ALERTS

Request recent alert history from server buffer.

```
Format:   GET_ALERTS
Response: OK <count> alerts
          ALERT <severity>|<type>|<message>
          ...
```

### LIST USERS

List connected clients (ENGINEER only).

```
Format:   LIST USERS
Response: OK <count> users
          USER <ip>:<port> ROLE=<role> NAME=<name>
          ...
          ERR forbidden
```

### MODIFY_RPM

Change equipment RPM by a delta value (ENGINEER only).

```
Format:   MODIFY_RPM <delta>
Response: OK rpm=<new_value>
          ERR forbidden
          ERR invalid_value
          ERR system_load_too_low
          ERR rpm_at_minimum
          ERR rpm_at_maximum
```

### ADJUST_HEADING

Rotate equipment heading (ENGINEER only).

```
Format:   ADJUST_HEADING <LEFT|RIGHT>
Response: OK heading=<N|E|S|W>
          ERR forbidden
          ERR invalid_value
```

### SENSOR_DATA

Sensor clients publish readings to the server.

```
Format:   SENSOR_DATA <equipment_id>|<sensor_type>|<value>|<unit>|<timestamp>
Response: OK sensor_received
Example:  SENSOR_DATA PUMP-001|pressure|48.5|bar|2026-04-01 08:33:34
```

### QUIT

Close the session.

```
Format:   QUIT
Response: BYE
```

---

## Server → Client messages

### METRIC

Periodic equipment metrics broadcast (every 5 seconds).

```
Format:  METRIC rpm=<int>|load=<int>|temp=<int>|pressure=<int>|heading=<char>|ts=<timestamp>
Example: METRIC rpm=2500|load=78|temp=45|pressure=52|heading=N|ts=2026-04-01 08:33:51
```

### ALERT

Anomaly notification broadcast.

```
Format:  ALERT <severity>|<type>|<message>
Example: ALERT critical|temp_high|Coolant temperature at 92 C — exceeds critical limit
         ALERT warning|load_low|System load at 18% — below warning threshold
```

### STATUS

Response to GET_STATUS command.

```
Format:  STATUS rpm=<int>|load=<int>|temp=<int>|pressure=<int>|heading=<char>
```

### OK

Positive response.

```
Format:  OK <message>
```

### ERR

Error response.

```
Format:  ERR <reason>
```

### BYE

Session close confirmation.

```
Format:  BYE
```

---

## Error codes

| Code | Meaning |
|------|---------|
| `invalid_credentials` | Wrong username/password (rejected by external identity service) |
| `forbidden` | Command requires ENGINEER role |
| `invalid_value` | Argument out of range or malformed |
| `unknown_command` | Command not recognized |
| `system_load_too_low` | Cannot increase RPM with load below 10% |
| `rpm_at_minimum` | RPM already at 0 |
| `rpm_at_maximum` | RPM already at 5000 |

---

## Alert system

### Severities

| Severity | Meaning |
|----------|---------|
| `warning` | Value approaching limit, operator should be aware |
| `critical` | Value exceeded safe range, immediate attention needed |

### Thresholds

| Type | Warning | Critical |
|------|---------|----------|
| `rpm_high` | RPM > 4000 | RPM > 4500 |
| `load_low` | Load < 20% | Load < 10% |
| `temp_high` | Temp > 70°C | Temp > 85°C |
| `pressure_high` | Pressure > 80 bar | Pressure > 90 bar |
| `pressure_low` | Pressure < 15 bar | Pressure < 8 bar |

---

## Value ranges

| Metric | Min | Max | Unit |
|--------|-----|-----|------|
| RPM | 0 | 5000 | rev/min |
| System load | 0 | 100 | % |
| Coolant temperature | 0 | 100 | °C |
| Hydraulic pressure | 0 | 100 | bar |
| Heading | N, E, S, W | — | cardinal |

---

## Simulated sensors

| Equipment ID | Sensor type | Unit | Typical range |
|-------------|-------------|------|---------------|
| PUMP-001 | pressure | bar | 30–70 |
| MOTOR-001 | rpm | rev/min | 1500–3500 |
| COOLER-001 | temperature | °C | 35–55 |
| VIBR-001 | vibration | mm/s | 1–5 |
| ENERGY-001 | energy | kW | 25–50 |
| HUMIDITY-001 | humidity | % | 40–70 |

---

## HTTP Web Interface

The server includes an embedded HTTP server on port TCP_PORT + 80 (default: 9080).

### Endpoints

| Method | Path | Auth | Description |
|--------|------|------|-------------|
| GET | `/` | No | Login page |
| GET | `/login?user=X&pass=Y` | No | Authenticate and redirect to dashboard |
| GET | `/dashboard` | Cookie | System dashboard with metrics, sensors, alerts |
| GET | `/api/status` | No | JSON equipment status |
| GET | `/api/alerts` | No | JSON recent alerts |
| GET | `/api/sensors` | No | JSON sensor fleet info |

### HTTP features

- Correct HTTP/1.1 headers (Content-Type, Content-Length, Connection)
- Status codes: 200 OK, 302 Found (redirect), 400 Bad Request, 404 Not Found, 405 Method Not Allowed
- Cookie-based session management for dashboard access
- Auto-refresh dashboard every 5 seconds
- JSON API for programmatic access

---

## External identity service

User credentials are stored in an external file (`users.conf`), not in the server binary.

### File format

```
# username:password:role
engineer:eng2025:ENGINEER
supervisor:sup2025:SUPERVISOR
operator1:op123:SUPERVISOR
```

The file is read on every authentication attempt, allowing credential updates without server restart. In Docker, this file is mounted as a volume.

---

## Session example

```
Client                              Server
──────                              ──────

                                ←   OK Welcome to IoT Equipment Monitor. Commands: ...

HELLO name=operator-01          →
                                ←   OK hello operator-01

AUTHENTICATE engineer eng2025   →
                                ←   OK authenticated

GET_STATUS                      →
                                ←   STATUS rpm=0|load=100|temp=28|pressure=50|heading=N

MODIFY_RPM 500                  →
                                ←   OK rpm=500

                                ←   METRIC rpm=500|load=98|temp=29|pressure=35|heading=N|ts=2026-04-01 08:33:51

SENSOR_DATA PUMP-001|pressure|48.5|bar|2026-04-01 08:33:52  →
                                ←   OK sensor_received

ADJUST_HEADING RIGHT            →
                                ←   OK heading=E

                                ←   ALERT warning|load_low|System load at 18% — below warning threshold

GET_ALERTS                      →
                                ←   OK 1 alerts
                                ←   ALERT warning|load_low|System load at 18% — below warning threshold

QUIT                            →
                                ←   BYE
```

---

## Notes

- All numeric values are integers except sensor readings (float, 1 decimal)
- Timestamps use format: `YYYY-MM-DD HH:MM:SS`
- Commands are case-sensitive
- Fields within METRIC/STATUS lines are separated by `|`
- Key-value pairs use `=`
- Maximum 4096 bytes per message
- Server broadcasts METRIC to all clients every 5 seconds
- Server broadcasts ALERT only when thresholds are crossed
- DNS resolution is used for all service lookups (no hardcoded IPs in code)