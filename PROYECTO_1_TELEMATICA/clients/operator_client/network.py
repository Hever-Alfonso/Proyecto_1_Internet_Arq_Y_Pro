"""
network.py
----------
TCP connection and protocol handler for the operator client.

Manages:
  - Connection lifecycle (connect, disconnect, reconnect)
  - Authentication handshake (HELLO + AUTHENTICATE)
  - Sending commands (MODIFY_RPM, ADJUST_HEADING, GET_STATUS, etc.)
  - Background receiver thread that parses incoming lines
  - Callbacks for metrics, alerts, status changes, and log messages

All server communication is line-based (\\n terminated).
"""

import socket
import threading
import time
from datetime import datetime


class NetworkManager:
    """Handles all TCP communication with the monitoring server."""

    def __init__(self, host="localhost", port=9000,
                 name="operator-python", user="engineer",
                 password="eng2026"):
        self.host = host
        self.port = port
        self.name = name
        self.user = user
        self.password = password

        self._socket = None
        self._connected = False
        self._receiver_thread = None
        self._should_receive = False
        self._lock = threading.Lock()

        # Callbacks (set by the dashboard)
        self.on_metric = None       # fn(dict)
        self.on_alert = None        # fn(severity, alert_type, message)
        self.on_status_line = None  # fn(dict)
        self.on_log = None          # fn(str)
        self.on_connection_change = None  # fn(bool)

    # ── Connection lifecycle ──

    def connect(self):
        """Establish TCP connection and perform handshake."""
        if self._connected:
            self._log("Already connected")
            return True

        try:
            self._socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            self._socket.settimeout(10)

            self._log(f"Connecting to {self.host}:{self.port}...")
            self._socket.connect((self.host, self.port))

            # Read welcome
            welcome = self._read_line()
            self._log(f"Server: {welcome}")

            # HELLO
            self._send_line(f"HELLO name={self.name}")
            hello_resp = self._read_line()
            self._log(f"Server: {hello_resp}")

            # AUTHENTICATE
            self._send_line(f"AUTHENTICATE {self.user} {self.password}")
            auth_resp = self._read_line()
            self._log(f"Server: {auth_resp}")

            if not auth_resp.startswith("OK"):
                self._log(f"Authentication failed: {auth_resp}")
                self._cleanup_socket()
                return False

            # Remove timeout for normal operation
            self._socket.settimeout(None)

            self._connected = True
            self._notify_connection(True)
            self._log("Connected and authenticated")

            # Start background receiver
            self._start_receiver()
            return True

        except socket.timeout:
            self._log("Connection timeout")
            self._cleanup_socket()
            return False
        except ConnectionRefusedError:
            self._log("Connection refused — is the server running?")
            self._cleanup_socket()
            return False
        except Exception as e:
            self._log(f"Connection error: {e}")
            self._cleanup_socket()
            return False

    def disconnect(self):
        """Send QUIT and close connection cleanly."""
        if not self._connected:
            return

        self._log("Disconnecting...")
        self._should_receive = False

        try:
            if self._socket:
                self._send_line("QUIT")
                time.sleep(0.1)
        except Exception:
            pass

        self._cleanup_socket()
        self._connected = False
        self._notify_connection(False)
        self._log("Disconnected")

    def is_connected(self):
        """Return current connection state."""
        return self._connected

    # ── Command senders ──

    def send_command(self, command):
        """Send a raw command string to the server."""
        if not self._connected or not self._socket:
            self._log("Cannot send — not connected")
            return False

        try:
            self._send_line(command)
            self._log(f"Sent: {command}")
            return True
        except Exception as e:
            self._log(f"Send error: {e}")
            self._handle_disconnect()
            return False

    def request_status(self):
        """Send GET_STATUS command."""
        return self.send_command("GET_STATUS")

    def request_alerts(self):
        """Send GET_ALERTS command."""
        return self.send_command("GET_ALERTS")

    def modify_rpm(self, delta):
        """Send MODIFY_RPM <delta> command."""
        return self.send_command(f"MODIFY_RPM {delta}")

    def adjust_heading(self, direction):
        """Send ADJUST_HEADING <LEFT|RIGHT> command."""
        return self.send_command(f"ADJUST_HEADING {direction}")

    # ── Background receiver ──

    def _start_receiver(self):
        """Launch the background thread that reads server messages."""
        self._should_receive = True
        self._receiver_thread = threading.Thread(
            target=self._receive_loop, daemon=True
        )
        self._receiver_thread.start()

    def _receive_loop(self):
        """Read lines from server and dispatch to callbacks."""
        buffer = ""

        while self._should_receive and self._connected:
            try:
                data = self._socket.recv(4096)
                if not data:
                    self._log("Server closed connection")
                    self._handle_disconnect()
                    break

                buffer += data.decode("utf-8", errors="replace")

                while "\n" in buffer:
                    line, buffer = buffer.split("\n", 1)
                    line = line.strip()
                    if line:
                        self._dispatch_line(line)

            except socket.timeout:
                continue
            except Exception as e:
                if self._should_receive:
                    self._log(f"Receiver error: {e}")
                    self._handle_disconnect()
                break

    def _dispatch_line(self, line):
        """Parse a single server line and call the appropriate callback."""

        if line.startswith("METRIC "):
            self._parse_metric(line)

        elif line.startswith("ALERT "):
            self._parse_alert(line)

        elif line.startswith("STATUS "):
            self._parse_status(line)

        elif line.startswith("OK") or line.startswith("ERR") or line.startswith("BYE"):
            self._log(f"Server: {line}")

        else:
            self._log(f"Server: {line}")

    # ── Parsers ──

    def _parse_metric(self, line):
        """Parse: METRIC rpm=X|load=X|temp=X|pressure=X|heading=X|ts=X"""
        try:
            body = line[len("METRIC "):]
            parts = body.split("|")
            data = {}
            for part in parts:
                if "=" in part:
                    key, value = part.split("=", 1)
                    data[key.strip()] = value.strip()

            if self.on_metric:
                self.on_metric(data)

        except Exception as e:
            self._log(f"Metric parse error: {e}")

    def _parse_alert(self, line):
        """Parse: ALERT <severity>|<type>|<message>"""
        try:
            body = line[len("ALERT "):]
            parts = body.split("|", 2)
            if len(parts) >= 3:
                severity = parts[0].strip()
                alert_type = parts[1].strip()
                message = parts[2].strip()

                if self.on_alert:
                    self.on_alert(severity, alert_type, message)

                self._log(f"ALERT [{severity}] {alert_type}: {message}")

        except Exception as e:
            self._log(f"Alert parse error: {e}")

    def _parse_status(self, line):
        """Parse: STATUS rpm=X|load=X|temp=X|pressure=X|heading=X"""
        try:
            body = line[len("STATUS "):]
            parts = body.split("|")
            data = {}
            for part in parts:
                if "=" in part:
                    key, value = part.split("=", 1)
                    data[key.strip()] = value.strip()

            if self.on_status_line:
                self.on_status_line(data)

        except Exception as e:
            self._log(f"Status parse error: {e}")

    # ── Internal helpers ──

    def _send_line(self, line):
        """Send a line to the server (thread-safe)."""
        with self._lock:
            if self._socket:
                self._socket.sendall(f"{line}\n".encode("utf-8"))

    def _read_line(self):
        """Read a single line from the server (blocking, with timeout)."""
        if not self._socket:
            return ""

        data = b""
        while True:
            byte = self._socket.recv(1)
            if not byte:
                return data.decode("utf-8", errors="replace").strip()
            if byte == b"\n":
                return data.decode("utf-8", errors="replace").strip()
            data += byte

    def _cleanup_socket(self):
        """Close socket and release resources."""
        if self._socket:
            try:
                self._socket.close()
            except Exception:
                pass
            self._socket = None

    def _handle_disconnect(self):
        """Called when connection is unexpectedly lost."""
        self._should_receive = False
        self._connected = False
        self._cleanup_socket()
        self._notify_connection(False)

    def _notify_connection(self, connected):
        """Notify the dashboard of connection state change."""
        if self.on_connection_change:
            self.on_connection_change(connected)

    def _log(self, message):
        """Send a log message to the dashboard."""
        ts = datetime.now().strftime("%H:%M:%S")
        formatted = f"[{ts}] {message}"
        if self.on_log:
            self.on_log(formatted)
        else:
            print(formatted)