"""
main.py
-------
Entry point for the IoT Operator Client (Python).

Usage:
    python main.py <host> <port>
    python main.py localhost 9000
    python main.py                   (defaults to localhost:9000)

Creates the NetworkManager, AlertManager, and OperatorDashboard,
then starts the GUI event loop.
"""

import sys
import customtkinter as ctk
from network import NetworkManager
from alerts import AlertManager
from dashboard import OperatorDashboard


def _ask_credentials():
    """Show a login dialog and return (user, password), or exit if cancelled."""
    result = {}

    win = ctk.CTk()
    win.title("Login")
    win.geometry("320x260")
    win.resizable(False, False)

    ctk.CTkLabel(win, text="IoT Equipment Monitor", font=ctk.CTkFont(size=16, weight="bold")).pack(pady=(24, 4))
    ctk.CTkLabel(win, text="Ingrese sus credenciales", text_color="gray").pack(pady=(0, 16))

    ctk.CTkLabel(win, text="Usuario", anchor="w").pack(padx=40, fill="x")
    user_entry = ctk.CTkEntry(win)
    user_entry.pack(padx=40, fill="x", pady=(2, 10))

    ctk.CTkLabel(win, text="Contraseña", anchor="w").pack(padx=40, fill="x")
    pass_entry = ctk.CTkEntry(win, show="*")
    pass_entry.pack(padx=40, fill="x", pady=(2, 16))

    def on_login():
        u = user_entry.get().strip()
        p = pass_entry.get().strip()
        if u and p:
            result["user"] = u
            result["password"] = p
            win.destroy()

    ctk.CTkButton(win, text="Ingresar", command=on_login).pack(padx=40, fill="x")
    user_entry.bind("<Return>", lambda e: on_login())
    pass_entry.bind("<Return>", lambda e: on_login())
    user_entry.focus()

    win.mainloop()

    if "user" not in result:
        sys.exit(0)

    return result["user"], result["password"]


DEFAULT_HOST = 3.239.101.186
DEFAULT_PORT = 9000


def main():
    # Parse CLI arguments
    host = DEFAULT_HOST
    port = DEFAULT_PORT

    if len(sys.argv) >= 3:
        host = sys.argv[1]
        try:
            port = int(sys.argv[2])
        except ValueError:
            print(f"Error: invalid port '{sys.argv[2]}'")
            sys.exit(1)
    elif len(sys.argv) == 2:
        host = sys.argv[1]

    print("=" * 48)
    print("  IoT Operator Client (Python)")
    print("=" * 48)
    print(f"  Server:  {host}:{port}")
    print("=" * 48)

    # Ask credentials before opening dashboard
    user, password = _ask_credentials()

    # Create modules
    network = NetworkManager(
        host=host,
        port=port,
        name="operator-python",
        user=user,
        password=password,
    )

    alert_mgr = AlertManager()

    # Create and run dashboard
    app = OperatorDashboard(network, alert_mgr)
    app.mainloop()


if __name__ == "__main__":
    main()
