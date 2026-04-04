/*
 * reconnect.go
 * ------------
 * Automatic reconnection with exponential backoff.
 *
 * The ReconnectManager wraps a ServerConnection and monitors its
 * health.  When a disconnect is detected (either from a send error
 * or the receiver goroutine exiting), the manager:
 *
 *   1. Waits an increasing delay (2s, 4s, 8s, … capped at 30s)
 *   2. Attempts to re-establish the connection
 *   3. On success, resets the delay and resumes normal operation
 *   4. On failure, increments the delay and tries again
 *
 * The manager runs in its own goroutine and communicates status
 * through a callback.
 */

 package main

 import (
	 "fmt"
	 "time"
 )
 
 // ConnectionStatus represents the current state reported to the caller.
 type ConnectionStatus int
 
 const (
	 StatusDisconnected ConnectionStatus = iota
	 StatusConnecting
	 StatusConnected
 )
 
 func (s ConnectionStatus) String() string {
	 switch s {
	 case StatusDisconnected:
		 return "DISCONNECTED"
	 case StatusConnecting:
		 return "CONNECTING"
	 case StatusConnected:
		 return "CONNECTED"
	 default:
		 return "UNKNOWN"
	 }
 }
 
 // ReconnectConfig holds the backoff parameters.
 type ReconnectConfig struct {
	 InitialDelay time.Duration
	 MaxDelay     time.Duration
	 Multiplier   float64
 }
 
 // DefaultReconnectConfig returns sensible defaults.
 func DefaultReconnectConfig() ReconnectConfig {
	 return ReconnectConfig{
		 InitialDelay: 2 * time.Second,
		 MaxDelay:     30 * time.Second,
		 Multiplier:   2.0,
	 }
 }
 
 // ReconnectManager manages connection lifecycle and automatic recovery.
 type ReconnectManager struct {
	 host     string
	 port     string
	 name     string
	 user     string
	 password string
	 config   ReconnectConfig
 
	 conn     *ServerConnection
	 running  bool
	 stopCh   chan struct{}
 
	 OnStatus  func(status ConnectionStatus)
	 OnMessage func(line string)
 }
 
 // NewReconnectManager creates a manager with the given server parameters.
 func NewReconnectManager(host, port, name, user, password string,
	 config ReconnectConfig) *ReconnectManager {
 
	 return &ReconnectManager{
		 host:     host,
		 port:     port,
		 name:     name,
		 user:     user,
		 password: password,
		 config:   config,
		 stopCh:   make(chan struct{}),
	 }
 }
 
 // GetConnection returns the current active connection (may be nil).
 func (rm *ReconnectManager) GetConnection() *ServerConnection {
	 return rm.conn
 }
 
 // Start launches the reconnection loop in a goroutine.
 func (rm *ReconnectManager) Start() {
	 rm.running = true
	 go rm.reconnectLoop()
 }
 
 // Stop signals the manager to shut down and close the connection.
 func (rm *ReconnectManager) Stop() {
	 rm.running = false
	 close(rm.stopCh)
 
	 if rm.conn != nil {
		 rm.conn.Close()
	 }
 }
 
 // ── Internal ──
 
 func (rm *ReconnectManager) reconnectLoop() {
	 delay := rm.config.InitialDelay
	 attempt := 0
 
	 for rm.running {
		 attempt++
 
		 rm.reportStatus(StatusConnecting)
		 rm.log("connection attempt #%d to %s:%s", attempt, rm.host, rm.port)
 
		 // Create a fresh connection
		 sc := NewServerConnection(rm.host, rm.port, rm.name, rm.user, rm.password)
		 sc.OnMessage = rm.OnMessage
 
		 err := sc.Connect()
		 if err != nil {
			 rm.log("connection failed: %v", err)
			 rm.reportStatus(StatusDisconnected)
 
			 // Backoff wait (interruptible)
			 if !rm.waitWithStop(delay) {
				 return // stopped
			 }
 
			 // Increase delay
			 delay = time.Duration(float64(delay) * rm.config.Multiplier)
			 if delay > rm.config.MaxDelay {
				 delay = rm.config.MaxDelay
			 }
			 continue
		 }
 
		 // Connected successfully
		 rm.conn = sc
		 rm.reportStatus(StatusConnected)
		 rm.log("connected successfully")
 
		 // Reset backoff
		 delay = rm.config.InitialDelay
		 attempt = 0
 
		 // Start receiver and remove read deadline for long-lived reads
		 sc.conn.SetReadDeadline(time.Time{})
		 sc.StartReceiver()
 
		 // Wait until disconnected
		 for rm.running && sc.IsConnected() {
			 if !rm.waitWithStop(1 * time.Second) {
				 return // stopped
			 }
		 }
 
		 if !rm.running {
			 return
		 }
 
		 rm.log("connection lost — will reconnect")
		 rm.reportStatus(StatusDisconnected)
		 rm.conn = nil
 
		 // Brief pause before reconnecting
		 if !rm.waitWithStop(rm.config.InitialDelay) {
			 return
		 }
	 }
 }
 
 // waitWithStop sleeps for the given duration but returns early if
 // the stop channel is closed.  Returns false if stopped.
 func (rm *ReconnectManager) waitWithStop(d time.Duration) bool {
	 select {
	 case <-time.After(d):
		 return true
	 case <-rm.stopCh:
		 return false
	 }
 }
 
 func (rm *ReconnectManager) reportStatus(s ConnectionStatus) {
	 if rm.OnStatus != nil {
		 rm.OnStatus(s)
	 }
 }
 
 func (rm *ReconnectManager) log(format string, args ...interface{}) {
	 ts := time.Now().Format("15:04:05")
	 msg := fmt.Sprintf(format, args...)
	 fmt.Printf("[%s] RECONNECT: %s\n", ts, msg)
 }