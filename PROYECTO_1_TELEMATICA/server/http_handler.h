/*
 * http_handler.h
 * --------------
 * Embedded HTTP server for the IoT monitoring system.
 *
 * Required by project specification:
 *   - Basic HTTP server integrated into the system
 *   - Interpret HTTP headers correctly
 *   - Handle GET requests
 *   - Return appropriate status codes (200, 400, 404, 500)
 *   - Allow users to: log in, view system status, view active sensors
 *
 * The HTTP server runs on its own port (default: TCP_PORT + 80,
 * e.g. 9080 if the main server is on 9000) in a dedicated thread.
 * Each HTTP request is handled in a short-lived thread.
 *
 * Endpoints:
 *   GET /              → login page
 *   GET /login?user=X&pass=Y → authenticate and redirect
 *   GET /dashboard     → system status + active sensors (requires auth)
 *   GET /api/status    → JSON equipment status
 *   GET /api/alerts    → JSON recent alerts
 *   GET /api/sensors   → JSON connected sensors count
 *   (anything else)    → 404 Not Found
 */

#ifndef HTTP_HANDLER_H
#define HTTP_HANDLER_H

/* Start the HTTP server thread on the given port.
 * Returns 0 on success, -1 on failure. */
int  http_server_start(int port);

/* Signal the HTTP server to stop. */
void http_server_stop(void);

#endif /* HTTP_HANDLER_H */