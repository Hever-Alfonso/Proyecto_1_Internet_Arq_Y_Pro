"""
main.py
-------
Entry point for the IoT Operator Client (Python).

Usage:
    python main.py <host> <port>
    python main.py 127.0.0.1 9000
    python main.py                   (defaults to 127.0.0.1:9000)

Creates the NetworkManager, AlertManager, and OperatorDashboard,
then starts the GUI event loop.
"""

import sys
from network import NetworkManager
from alerts import AlertManager
from dashboard import OperatorDashboard


DEFAULT_HOST = "127.0.0.1"
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

    # Create modules
    network = NetworkManager(
        host=host,
        port=port,
        name="operator-python",
        user="engineer",
        password="eng2026",
    )

    alert_mgr = AlertManager()

    # Create and run dashboard
    app = OperatorDashboard(network, alert_mgr)
    app.mainloop()


if __name__ == "__main__":
    main()