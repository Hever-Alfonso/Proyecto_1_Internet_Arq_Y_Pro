/*
 * protocol.go
 * -----------
 * TCP connection and protocol handling for the sensor client.
 *
 * Responsibilities:
 *   - Establish TCP connection to the server
 *   - Perform handshake: HELLO + AUTHENTICATE
 *   - Send SENSOR_DATA lines
 *   - Receive and log server responses (OK, ERR, METRIC, ALERT)
 *   - Clean disconnect with QUIT
 *
 * All network I/O is line-based (\n terminated), matching the
 * server's text protocol.
 */

package main

import (
	"bufio"
	"fmt"
	"net"
	"strings"
	"sync"
	"time"
)

// ServerConnection manages the TCP link to the monitoring server.
type ServerConnection struct {
	host     string
	port     string
	name     string
	user     string
	password string

	conn   net.Conn
	reader *bufio.Reader
	mu     sync.Mutex // protects writes

	connected bool
	OnMessage func(line string) // callback for received lines
}

// NewServerConnection creates a connection configuration.
func NewServerConnection(host, port, name, user, password string) *ServerConnection {
	return &ServerConnection{
		host:     host,
		port:     port,
		name:     name,
		user:     user,
		password: password,
	}
}

// Connect establishes the TCP connection and performs the handshake.
func (sc *ServerConnection) Connect() error {
	addr := net.JoinHostPort(sc.host, sc.port)

	conn, err := net.DialTimeout("tcp", addr, 10*time.Second)
	if err != nil {
		return fmt.Errorf("dial failed: %w", err)
	}

	sc.conn = conn
	sc.reader = bufio.NewReader(conn)
	sc.connected = true

	// Read welcome message
	welcome, err := sc.readLine()
	if err != nil {
		sc.Close()
		return fmt.Errorf("reading welcome: %w", err)
	}
	sc.logReceived(welcome)

	// Send HELLO
	if err := sc.sendLine(fmt.Sprintf("HELLO name=%s", sc.name)); err != nil {
		sc.Close()
		return fmt.Errorf("sending HELLO: %w", err)
	}

	helloResp, err := sc.readLine()
	if err != nil {
		sc.Close()
		return fmt.Errorf("reading HELLO response: %w", err)
	}
	sc.logReceived(helloResp)

	// Send AUTHENTICATE
	if err := sc.sendLine(fmt.Sprintf("AUTHENTICATE %s %s", sc.user, sc.password)); err != nil {
		sc.Close()
		return fmt.Errorf("sending AUTHENTICATE: %w", err)
	}

	authResp, err := sc.readLine()
	if err != nil {
		sc.Close()
		return fmt.Errorf("reading AUTH response: %w", err)
	}
	sc.logReceived(authResp)

	if !strings.HasPrefix(authResp, "OK") {
		sc.Close()
		return fmt.Errorf("authentication failed: %s", authResp)
	}

	return nil
}

// SendSensorData sends a single sensor reading to the server.
func (sc *ServerConnection) SendSensorData(reading SensorReading) error {
	line := reading.FormatMetric()
	return sc.sendLine(line)
}

// StartReceiver launches a goroutine that reads lines from the server
// and passes them to the OnMessage callback.  Blocks until the
// connection is closed or an error occurs.
func (sc *ServerConnection) StartReceiver() {
	go func() {
		for sc.connected {
			line, err := sc.readLine()
			if err != nil {
				if sc.connected {
					fmt.Printf("[%s] receiver error: %v\n",
						time.Now().Format("15:04:05"), err)
				}
				sc.connected = false
				return
			}

			sc.logReceived(line)

			if sc.OnMessage != nil {
				sc.OnMessage(line)
			}
		}
	}()
}

// IsConnected reports whether the connection is still alive.
func (sc *ServerConnection) IsConnected() bool {
	return sc.connected
}

// Close sends QUIT and shuts down the connection.
func (sc *ServerConnection) Close() {
	if sc.conn == nil {
		return
	}

	sc.connected = false

	// Best-effort QUIT
	_ = sc.sendLine("QUIT")

	// Small delay to let server process QUIT
	time.Sleep(100 * time.Millisecond)

	sc.conn.Close()
	sc.conn = nil
}

// ── Internal helpers ──

func (sc *ServerConnection) sendLine(line string) error {
	sc.mu.Lock()
	defer sc.mu.Unlock()

	if sc.conn == nil {
		return fmt.Errorf("not connected")
	}

	// Set write deadline to avoid blocking forever
	sc.conn.SetWriteDeadline(time.Now().Add(5 * time.Second))

	_, err := fmt.Fprintf(sc.conn, "%s\n", line)
	if err != nil {
		sc.connected = false
		return fmt.Errorf("write failed: %w", err)
	}

	fmt.Printf("[%s] SENT: %s\n", time.Now().Format("15:04:05"), line)
	return nil
}

func (sc *ServerConnection) readLine() (string, error) {
	if sc.conn == nil {
		return "", fmt.Errorf("not connected")
	}

	// Set read deadline for the handshake phase; the receiver
	// goroutine will reset it to zero for long-lived reads.
	sc.conn.SetReadDeadline(time.Now().Add(15 * time.Second))

	line, err := sc.reader.ReadString('\n')
	if err != nil {
		return "", err
	}

	return strings.TrimSpace(line), nil
}

func (sc *ServerConnection) logReceived(line string) {
	fmt.Printf("[%s] RECV: %s\n", time.Now().Format("15:04:05"), line)
}