"""
dashboard.py
------------
Operator dashboard GUI for the IoT Equipment Monitoring System.

Built with customtkinter.  Replaces the old monolithic admin_client.py
with a structured layout:

  ┌─────────────────────────────────────────────────────┐
  │  IoT Equipment Monitor — Operator Dashboard         │
  ├──────────────────────┬──────────────────────────────┤
  │  METRICS PANEL       │  CONTROL PANEL               │
  │  - RPM gauge         │  - RPM controls (+/-500)     │
  │  - Load bar          │  - Heading controls (L/R)    │
  │  - Temperature       │  - GET_STATUS / GET_ALERTS   │
  │  - Pressure          │  - Connect / Disconnect      │
  │  - Heading compass   │                              │
  │  - Timestamp         ├──────────────────────────────┤
  │                      │  ALERTS PANEL                │
  ├──────────────────────┤  - Recent alerts list        │
  │  CONNECTION STATUS   │  - Severity indicators       │
  │  - Status indicator  │  - Acknowledge button        │
  │                      │  - Alert statistics          │
  ├──────────────────────┴──────────────────────────────┤
  │  COMMAND LOG                                        │
  └─────────────────────────────────────────────────────┘

All server interaction goes through NetworkManager.
All alert handling goes through AlertManager.
"""

import customtkinter as ctk
from datetime import datetime


class OperatorDashboard(ctk.CTk):
    """Main operator GUI window."""

    def __init__(self, network_manager, alert_manager):
        super().__init__()

        self.net = network_manager
        self.alerts = alert_manager

        # Window config
        self.title("IoT Equipment Monitor — Operator Dashboard")
        self.geometry("1050x750")
        self.minsize(900, 650)

        # Theme
        ctk.set_appearance_mode("dark")
        ctk.set_default_color_theme("blue")

        # Current metric values
        self._metrics = {
            "rpm": "0",
            "load": "100",
            "temp": "28",
            "pressure": "50",
            "heading": "N",
            "ts": "--:--:--",
        }

        # Wire callbacks
        self.net.on_metric = self._on_metric_received
        self.net.on_alert = self._on_alert_received
        self.net.on_status_line = self._on_status_received
        self.net.on_log = self._on_log_received
        self.net.on_connection_change = self._on_connection_changed
        self.alerts.on_new_alert = self._on_new_alert_display

        # Build UI
        self._build_layout()

        # Window close
        self.protocol("WM_DELETE_WINDOW", self._on_closing)

    # ══════════════════════════════════════════════════════
    #  Layout
    # ══════════════════════════════════════════════════════

    def _build_layout(self):
        """Construct the entire dashboard layout."""

        # Main container
        main = ctk.CTkFrame(self, fg_color="transparent")
        main.pack(fill="both", expand=True, padx=16, pady=16)

        # Title
        ctk.CTkLabel(
            main,
            text="IoT Equipment Monitor",
            font=ctk.CTkFont(size=26, weight="bold"),
        ).pack(pady=(0, 14))

        # Top row: metrics + controls/alerts
        top = ctk.CTkFrame(main, fg_color="transparent")
        top.pack(fill="both", expand=True)

        self._build_metrics_panel(top)
        self._build_right_column(top)

        # Bottom: command log
        self._build_log_panel(main)

    # ── Metrics panel (left) ──

    def _build_metrics_panel(self, parent):
        frame = ctk.CTkFrame(parent)
        frame.pack(side="left", fill="both", expand=True, padx=(0, 8))

        ctk.CTkLabel(
            frame, text="Equipment Metrics",
            font=ctk.CTkFont(size=18, weight="bold"),
        ).pack(pady=(12, 16))

        # Timestamp
        self.ts_label = self._metric_row(frame, "Timestamp", self._metrics["ts"])

        # RPM
        self.rpm_label = self._metric_row(frame, "RPM", "0 rev/min")

        # Load
        load_frame = ctk.CTkFrame(frame)
        load_frame.pack(fill="x", padx=14, pady=4)
        ctk.CTkLabel(load_frame, text="System Load:",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(side="left", padx=10, pady=8)
        self.load_label = ctk.CTkLabel(load_frame, text="100%",
                                        font=ctk.CTkFont(size=14))
        self.load_label.pack(side="right", padx=10, pady=8)

        self.load_bar = ctk.CTkProgressBar(frame)
        self.load_bar.pack(fill="x", padx=24, pady=(0, 8))
        self.load_bar.set(1.0)

        # Temperature
        self.temp_label = self._metric_row(frame, "Coolant Temp", "28 °C")

        # Pressure
        self.pressure_label = self._metric_row(frame, "Hydraulic Pressure", "50 bar")

        # Heading compass
        heading_frame = ctk.CTkFrame(frame)
        heading_frame.pack(fill="x", padx=14, pady=(12, 4))
        ctk.CTkLabel(
            heading_frame, text="Heading",
            font=ctk.CTkFont(size=16, weight="bold"),
        ).pack(pady=(8, 10))

        compass = ctk.CTkFrame(heading_frame, fg_color="transparent")
        compass.pack(pady=8)

        self.compass_labels = {}
        for direction, row, col in [("N", 0, 1), ("W", 1, 0), ("E", 1, 2), ("S", 2, 1)]:
            lbl = ctk.CTkLabel(
                compass, text=direction,
                font=ctk.CTkFont(size=18, weight="bold"),
                width=46, height=46,
                fg_color="gray25", corner_radius=10,
            )
            lbl.grid(row=row, column=col, padx=4, pady=4)
            self.compass_labels[direction] = lbl

        self.heading_center = ctk.CTkLabel(
            compass, text="N",
            font=ctk.CTkFont(size=22, weight="bold"),
            width=46, height=46,
            fg_color="#1f6aa5", corner_radius=10, text_color="white",
        )
        self.heading_center.grid(row=1, column=1, padx=4, pady=4)

        # Connection status
        status_frame = ctk.CTkFrame(frame)
        status_frame.pack(fill="x", padx=14, pady=(14, 10))
        ctk.CTkLabel(status_frame, text="Status:",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(side="left", padx=10, pady=8)
        self.status_label = ctk.CTkLabel(
            status_frame, text="● Disconnected",
            font=ctk.CTkFont(size=14), text_color="red",
        )
        self.status_label.pack(side="right", padx=10, pady=8)

    def _metric_row(self, parent, label_text, default_value):
        """Create a label-value row and return the value label."""
        row = ctk.CTkFrame(parent)
        row.pack(fill="x", padx=14, pady=4)
        ctk.CTkLabel(row, text=f"{label_text}:",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(side="left", padx=10, pady=8)
        val = ctk.CTkLabel(row, text=default_value,
                           font=ctk.CTkFont(size=14))
        val.pack(side="right", padx=10, pady=8)
        return val

    # ── Right column: controls + alerts ──

    def _build_right_column(self, parent):
        right = ctk.CTkFrame(parent, fg_color="transparent")
        right.pack(side="right", fill="both", expand=True, padx=(8, 0))

        self._build_control_panel(right)
        self._build_alerts_panel(right)

    def _build_control_panel(self, parent):
        frame = ctk.CTkFrame(parent)
        frame.pack(fill="x", pady=(0, 8))

        ctk.CTkLabel(
            frame, text="Control Panel",
            font=ctk.CTkFont(size=18, weight="bold"),
        ).pack(pady=(12, 14))

        # RPM controls
        rpm_section = ctk.CTkFrame(frame)
        rpm_section.pack(fill="x", padx=14, pady=4)
        ctk.CTkLabel(rpm_section, text="RPM Control",
                     font=ctk.CTkFont(size=14, weight="bold")).pack(pady=(8, 6))

        rpm_btns = ctk.CTkFrame(rpm_section, fg_color="transparent")
        rpm_btns.pack(fill="x", padx=16, pady=(0, 10))

        ctk.CTkButton(rpm_btns, text="RPM -500",
                      command=lambda: self._cmd_rpm(-500),
                      height=36, width=120).pack(side="left", padx=4)
        ctk.CTkButton(rpm_btns, text="RPM -100",
                      command=lambda: self._cmd_rpm(-100),
                      height=36, width=120).pack(side="left", padx=4)
        ctk.CTkButton(rpm_btns, text="RPM +100",
                      command=lambda: self._cmd_rpm(100),
                      height=36, width=120).pack(side="left", padx=4)
        ctk.CTkButton(rpm_btns, text="RPM +500",
                      command=lambda: self._cmd_rpm(500),
                      height=36, width=120).pack(side="left", padx=4)

        # Heading controls
        heading_btns = ctk.CTkFrame(frame, fg_color="transparent")
        heading_btns.pack(fill="x", padx=14, pady=4)

        ctk.CTkButton(heading_btns, text="Turn Left",
                      command=lambda: self._cmd_heading("LEFT"),
                      height=36).pack(side="left", fill="x", expand=True, padx=4)
        ctk.CTkButton(heading_btns, text="Turn Right",
                      command=lambda: self._cmd_heading("RIGHT"),
                      height=36).pack(side="right", fill="x", expand=True, padx=4)

        # Status / Alerts request buttons
        query_btns = ctk.CTkFrame(frame, fg_color="transparent")
        query_btns.pack(fill="x", padx=14, pady=4)

        ctk.CTkButton(query_btns, text="GET STATUS",
                      command=self.net.request_status,
                      height=34, fg_color="gray30").pack(side="left", fill="x", expand=True, padx=4)
        ctk.CTkButton(query_btns, text="GET ALERTS",
                      command=self.net.request_alerts,
                      height=34, fg_color="gray30").pack(side="right", fill="x", expand=True, padx=4)

        # Connect / Disconnect
        conn_btns = ctk.CTkFrame(frame, fg_color="transparent")
        conn_btns.pack(fill="x", padx=14, pady=(8, 12))

        self.connect_btn = ctk.CTkButton(
            conn_btns, text="CONNECT", command=self._cmd_connect,
            height=44, font=ctk.CTkFont(size=15, weight="bold"),
            fg_color="#2d8a4e", hover_color="#1e6b38",
        )
        self.connect_btn.pack(side="left", fill="x", expand=True, padx=4)

        self.disconnect_btn = ctk.CTkButton(
            conn_btns, text="DISCONNECT", command=self._cmd_disconnect,
            height=44, font=ctk.CTkFont(size=15, weight="bold"),
            fg_color="#a83232", hover_color="#7a2020", state="disabled",
        )
        self.disconnect_btn.pack(side="right", fill="x", expand=True, padx=4)

    # ── Alerts panel ──

    def _build_alerts_panel(self, parent):
        frame = ctk.CTkFrame(parent)
        frame.pack(fill="both", expand=True)

        header = ctk.CTkFrame(frame, fg_color="transparent")
        header.pack(fill="x", padx=14, pady=(10, 4))

        ctk.CTkLabel(
            header, text="Alerts",
            font=ctk.CTkFont(size=18, weight="bold"),
        ).pack(side="left")

        self.alert_count_label = ctk.CTkLabel(
            header, text="0 alerts",
            font=ctk.CTkFont(size=12), text_color="gray",
        )
        self.alert_count_label.pack(side="left", padx=(10, 0))

        ctk.CTkButton(
            header, text="Acknowledge All",
            command=self._cmd_acknowledge_alerts,
            height=28, width=130, font=ctk.CTkFont(size=12),
            fg_color="gray30",
        ).pack(side="right")

        self.alerts_textbox = ctk.CTkTextbox(
            frame, height=160, font=ctk.CTkFont(family="Courier", size=12),
        )
        self.alerts_textbox.pack(fill="both", expand=True, padx=14, pady=(4, 12))

    # ── Log panel (bottom) ──

    def _build_log_panel(self, parent):
        frame = ctk.CTkFrame(parent)
        frame.pack(fill="x", pady=(8, 0))

        ctk.CTkLabel(
            frame, text="Command Log",
            font=ctk.CTkFont(size=14, weight="bold"),
        ).pack(pady=(8, 4), padx=14, anchor="w")

        self.log_textbox = ctk.CTkTextbox(
            frame, height=110, font=ctk.CTkFont(family="Courier", size=11),
        )
        self.log_textbox.pack(fill="x", padx=14, pady=(0, 10))

    # ══════════════════════════════════════════════════════
    #  Callbacks from NetworkManager
    # ══════════════════════════════════════════════════════

    def _on_metric_received(self, data):
        """Called from receiver thread when a METRIC line arrives."""
        self._metrics.update(data)
        self.after(0, self._refresh_metrics_display)

    def _on_status_received(self, data):
        """Called when a STATUS response arrives."""
        self._metrics.update(data)
        self.after(0, self._refresh_metrics_display)

    def _on_alert_received(self, severity, alert_type, message):
        """Called from receiver thread when an ALERT line arrives."""
        self.alerts.add_alert(severity, alert_type, message)

    def _on_new_alert_display(self, record):
        """Called by AlertManager when a new alert is stored."""
        self.after(0, lambda: self._append_alert(record))

    def _on_log_received(self, message):
        """Called from network for log messages."""
        self.after(0, lambda: self._append_log(message))

    def _on_connection_changed(self, connected):
        """Called when connection state changes."""
        self.after(0, lambda: self._update_connection_ui(connected))

    # ══════════════════════════════════════════════════════
    #  UI update methods (always run on main thread)
    # ══════════════════════════════════════════════════════

    def _refresh_metrics_display(self):
        """Update all metric widgets from self._metrics."""
        m = self._metrics

        # Timestamp
        self.ts_label.configure(text=m.get("ts", "--:--:--"))

        # RPM
        rpm = m.get("rpm", "0")
        self.rpm_label.configure(text=f"{rpm} rev/min")

        # Load
        try:
            load = int(m.get("load", "0"))
            self.load_label.configure(text=f"{load}%")
            self.load_bar.set(load / 100.0)

            if load < 15:
                self.load_label.configure(text_color="red")
            elif load < 30:
                self.load_label.configure(text_color="orange")
            else:
                self.load_label.configure(text_color="green")
        except ValueError:
            pass

        # Temperature
        try:
            temp = int(m.get("temp", "0"))
            self.temp_label.configure(text=f"{temp} °C")

            if temp > 85:
                self.temp_label.configure(text_color="red")
            elif temp > 70:
                self.temp_label.configure(text_color="orange")
            else:
                self.temp_label.configure(text_color="white")
        except ValueError:
            pass

        # Pressure
        try:
            pressure = int(m.get("pressure", "0"))
            self.pressure_label.configure(text=f"{pressure} bar")

            if pressure > 90 or pressure < 8:
                self.pressure_label.configure(text_color="red")
            elif pressure > 80 or pressure < 15:
                self.pressure_label.configure(text_color="orange")
            else:
                self.pressure_label.configure(text_color="white")
        except ValueError:
            pass

        # Heading
        heading = m.get("heading", "N")
        self.heading_center.configure(text=heading)

        for d, lbl in self.compass_labels.items():
            if d == heading:
                lbl.configure(fg_color="#2d8a4e")
            else:
                lbl.configure(fg_color="gray25")

    def _update_connection_ui(self, connected):
        """Update connect/disconnect buttons and status label."""
        if connected:
            self.status_label.configure(text="● Connected", text_color="green")
            self.connect_btn.configure(state="disabled")
            self.disconnect_btn.configure(state="normal")
        else:
            self.status_label.configure(text="● Disconnected", text_color="red")
            self.connect_btn.configure(state="normal")
            self.disconnect_btn.configure(state="disabled")

    def _append_alert(self, record):
        """Add an alert to the alerts textbox."""
        line = record.format_display() + "\n"

        self.alerts_textbox.insert("end", line)
        self.alerts_textbox.see("end")

        # Update count
        stats = self.alerts.get_stats()
        total = stats["total"]
        crits = stats["total_criticals"]
        self.alert_count_label.configure(
            text=f"{total} alerts ({crits} critical)"
        )

        if record.is_critical():
            self.alert_count_label.configure(text_color="red")

    def _append_log(self, message):
        """Add a message to the command log."""
        self.log_textbox.insert("end", message + "\n")
        self.log_textbox.see("end")

    # ══════════════════════════════════════════════════════
    #  Command handlers
    # ══════════════════════════════════════════════════════

    def _cmd_connect(self):
        self.net.connect()

    def _cmd_disconnect(self):
        self.net.disconnect()

    def _cmd_rpm(self, delta):
        self.net.modify_rpm(delta)

    def _cmd_heading(self, direction):
        self.net.adjust_heading(direction)

    def _cmd_acknowledge_alerts(self):
        self.alerts.acknowledge_all()
        self._append_log(f"[{datetime.now().strftime('%H:%M:%S')}] "
                         f"All alerts acknowledged")

    def _on_closing(self):
        """Handle window close."""
        if self.net.is_connected():
            self.net.disconnect()
        self.after(200, self.destroy)