"""
alerts.py
---------
Alert management for the operator client.

This module is entirely NEW — the original project had no alert
handling on the client side.  It:

  - Stores incoming alerts in a local history list
  - Provides filtering by severity and type
  - Tracks statistics (total, warnings, criticals per type)
  - Formats alerts for display in the dashboard
  - Supports a maximum history size to bound memory usage

Alert record format (from server):
  ALERT <severity>|<type>|<message>

Severities: warning, critical
Types: rpm_high, load_low, temp_high, pressure_high, pressure_low
"""

from datetime import datetime
from collections import defaultdict
from threading import Lock


class AlertRecord:
    """Single alert received from the server."""

    def __init__(self, severity, alert_type, message, timestamp=None):
        self.severity = severity        # "warning" or "critical"
        self.alert_type = alert_type    # e.g. "temp_high"
        self.message = message          # human-readable description
        self.timestamp = timestamp or datetime.now()
        self.acknowledged = False

    def format_display(self):
        """Format for showing in the dashboard alert list."""
        ts = self.timestamp.strftime("%H:%M:%S")
        sev = self.severity.upper()
        ack = " [ACK]" if self.acknowledged else ""
        return f"[{ts}] {sev} | {self.alert_type}: {self.message}{ack}"

    def format_short(self):
        """Short format for notification popups."""
        sev = "⚠" if self.severity == "warning" else "🔴"
        return f"{sev} {self.alert_type}: {self.message}"

    def is_critical(self):
        return self.severity == "critical"

    def is_warning(self):
        return self.severity == "warning"


class AlertManager:
    """Manages alert history and statistics for the operator dashboard."""

    MAX_HISTORY = 200

    def __init__(self):
        self._history = []
        self._lock = Lock()
        self._stats = defaultdict(lambda: {"warning": 0, "critical": 0})
        self._total_warnings = 0
        self._total_criticals = 0

        # Callback for new alert notification
        self.on_new_alert = None  # fn(AlertRecord)

    def add_alert(self, severity, alert_type, message):
        """
        Process a new alert from the server.

        Args:
            severity: "warning" or "critical"
            alert_type: e.g. "temp_high", "load_low"
            message: human-readable description
        """
        record = AlertRecord(severity, alert_type, message)

        with self._lock:
            self._history.append(record)

            # Trim history if too large
            if len(self._history) > self.MAX_HISTORY:
                self._history = self._history[-self.MAX_HISTORY:]

            # Update stats
            if severity in ("warning", "critical"):
                self._stats[alert_type][severity] += 1

            if severity == "warning":
                self._total_warnings += 1
            elif severity == "critical":
                self._total_criticals += 1

        # Notify dashboard
        if self.on_new_alert:
            self.on_new_alert(record)

        return record

    def get_history(self, limit=50):
        """Return the most recent alerts (newest first)."""
        with self._lock:
            return list(reversed(self._history[-limit:]))

    def get_unacknowledged(self):
        """Return all alerts that haven't been acknowledged."""
        with self._lock:
            return [a for a in self._history if not a.acknowledged]

    def get_by_severity(self, severity, limit=50):
        """Filter history by severity level."""
        with self._lock:
            filtered = [a for a in self._history if a.severity == severity]
            return list(reversed(filtered[-limit:]))

    def get_by_type(self, alert_type, limit=50):
        """Filter history by alert type."""
        with self._lock:
            filtered = [a for a in self._history
                        if a.alert_type == alert_type]
            return list(reversed(filtered[-limit:]))

    def get_criticals(self, limit=50):
        """Shortcut for getting critical alerts only."""
        return self.get_by_severity("critical", limit)

    def acknowledge_all(self):
        """Mark all current alerts as acknowledged."""
        with self._lock:
            for alert in self._history:
                alert.acknowledged = True

    def get_stats(self):
        """
        Return alert statistics.

        Returns:
            dict with:
                total_warnings: int
                total_criticals: int
                by_type: dict of {type: {warning: int, critical: int}}
        """
        with self._lock:
            return {
                "total_warnings": self._total_warnings,
                "total_criticals": self._total_criticals,
                "total": self._total_warnings + self._total_criticals,
                "by_type": dict(self._stats),
            }

    def get_last_critical(self):
        """Return the most recent critical alert, or None."""
        with self._lock:
            for alert in reversed(self._history):
                if alert.is_critical():
                    return alert
            return None

    def clear_history(self):
        """Reset all history and statistics."""
        with self._lock:
            self._history.clear()
            self._stats.clear()
            self._total_warnings = 0
            self._total_criticals = 0