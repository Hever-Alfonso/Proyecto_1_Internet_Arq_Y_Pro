/*
 * http_handler.c
 * --------------
 * Embedded HTTP server for the IoT monitoring system.
 *
 * Runs in its own accept loop on a separate port.
 * Each incoming HTTP connection is handled in a short-lived thread:
 *   1. Read the request line (GET /path HTTP/1.x)
 *   2. Parse headers (we only care about Cookie for auth)
 *   3. Route to the appropriate handler
 *   4. Send response with correct headers and status code
 *   5. Close connection
 *
 * No external libraries — raw HTTP over sockets.
 */

#define _GNU_SOURCE

#include "http_handler.h"
#include "equipment_state.h"
#include "alert_system.h"
#include "connection_handler.h"
#include "logger.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdatomic.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

/* ── Internal state ── */

static int          g_http_fd   = -1;
static atomic_int   g_http_stop = 0;
static pthread_t    g_http_tid;

/* ── HTTP helpers ── */

static void send_response(int fd, int status, const char *status_text,
                          const char *content_type, const char *extra_headers,
                          const char *body, int body_len) {
    char header[1024];
    int hlen = snprintf(header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %d\r\n"
        "Connection: close\r\n"
        "%s"
        "\r\n",
        status, status_text, content_type, body_len,
        extra_headers ? extra_headers : "");

    send(fd, header, hlen, MSG_NOSIGNAL);
    if (body && body_len > 0)
        send(fd, body, body_len, MSG_NOSIGNAL);
}

static void send_html(int fd, int status, const char *status_text,
                      const char *extra_headers, const char *html) {
    send_response(fd, status, status_text, "text/html; charset=utf-8",
                  extra_headers, html, strlen(html));
}

static void send_json(int fd, int status, const char *json) {
    send_response(fd, status, "OK", "application/json; charset=utf-8",
                  NULL, json, strlen(json));
}

/* ── URL parameter parser ── */

static int get_param(const char *query, const char *key,
                     char *out, size_t out_sz) {
    if (!query || !key) return 0;
    char search[128];
    snprintf(search, sizeof(search), "%s=", key);
    const char *p = strstr(query, search);
    if (!p) return 0;
    p += strlen(search);
    size_t i = 0;
    while (*p && *p != '&' && *p != ' ' && *p != '\r' && i < out_sz - 1) {
        out[i++] = *p++;
    }
    out[i] = '\0';
    return 1;
}

/* Check if request has valid auth cookie */
static int check_auth_cookie(const char *request) {
    const char *cookie = strstr(request, "Cookie:");
    if (!cookie) return 0;
    return strstr(cookie, "iot_auth=engineer") != NULL;
}

/* ── Page generators ── */

static const char *PAGE_LOGIN =
    "<!DOCTYPE html><html><head>"
    "<meta charset='utf-8'>"
    "<meta name='viewport' content='width=device-width,initial-scale=1'>"
    "<title>IoT Monitor - Login</title>"
    "<style>"
    "* { margin:0; padding:0; box-sizing:border-box; }"
    "body { font-family:sans-serif; background:#1a1a2e; color:#e0e0e0;"
    "       display:flex; justify-content:center; align-items:center; min-height:100vh; }"
    ".card { background:#16213e; padding:40px; border-radius:12px; width:380px;"
    "        box-shadow:0 4px 24px rgba(0,0,0,0.4); }"
    "h1 { text-align:center; margin-bottom:8px; color:#0ea5e9; }"
    "p.sub { text-align:center; margin-bottom:24px; color:#888; font-size:14px; }"
    "label { display:block; margin-bottom:4px; font-size:14px; color:#aaa; }"
    "input { width:100%%; padding:10px; margin-bottom:16px; border:1px solid #333;"
    "        border-radius:6px; background:#0f3460; color:#fff; font-size:14px; }"
    "input:focus { outline:none; border-color:#0ea5e9; }"
    "button { width:100%%; padding:12px; background:#0ea5e9; color:#fff; border:none;"
    "         border-radius:6px; font-size:16px; cursor:pointer; }"
    "button:hover { background:#0284c7; }"
    ".err { color:#f87171; text-align:center; margin-bottom:12px; font-size:13px; }"
    "</style></head><body>"
    "<div class='card'>"
    "<h1>IoT Monitor</h1>"
    "<p class='sub'>Equipment Monitoring System</p>"
    "%s"
    "<form action='/login' method='GET'>"
    "<label>Username</label><input name='user' required>"
    "<label>Password</label><input name='pass' type='password' required>"
    "<button type='submit'>Sign In</button>"
    "</form></div></body></html>";

static void generate_login_page(int fd, const char *error) {
    char html[4096];
    char err_div[256] = "";
    if (error && error[0])
        snprintf(err_div, sizeof(err_div), "<p class='err'>%s</p>", error);
    snprintf(html, sizeof(html), PAGE_LOGIN, err_div);
    send_html(fd, 200, "OK", NULL, html);
}

static void generate_dashboard(int fd) {
    equipment_snapshot_t snap;
    equipment_state_read(&snap);

    int client_count = connection_registry_count();

    #define MAX_DASH_ALERTS 10
    alert_record_t alerts[MAX_DASH_ALERTS];
    int alert_count = alert_history_copy(alerts, MAX_DASH_ALERTS);

    char alert_rows[2048] = "";
    int offset = 0;
    for (int i = 0; i < alert_count && offset < (int)sizeof(alert_rows) - 200; i++) {
        const char *color = alerts[i].severity == ALERT_CRITICAL ? "#f87171" : "#fbbf24";
        offset += snprintf(alert_rows + offset, sizeof(alert_rows) - offset,
            "<tr><td style='color:%s'>%s</td>"
            "<td>%s</td><td>%s</td><td>%s</td></tr>",
            color,
            alert_severity_string(alerts[i].severity),
            alerts[i].type, alerts[i].message, alerts[i].timestamp);
    }

    const char *hdg = heading_to_string(snap.equipment_heading);

    /* Color coding */
    const char *temp_color = snap.coolant_temperature > 85 ? "#f87171" :
                             snap.coolant_temperature > 70 ? "#fbbf24" : "#4ade80";
    const char *load_color = snap.system_load_percent < 10 ? "#f87171" :
                             snap.system_load_percent < 20 ? "#fbbf24" : "#4ade80";
    const char *pres_color = (snap.hydraulic_pressure > 90 || snap.hydraulic_pressure < 8) ? "#f87171" :
                             (snap.hydraulic_pressure > 80 || snap.hydraulic_pressure < 15) ? "#fbbf24" : "#4ade80";

    char html[8192];
    snprintf(html, sizeof(html),
        "<!DOCTYPE html><html><head>"
        "<meta charset='utf-8'>"
        "<meta name='viewport' content='width=device-width,initial-scale=1'>"
        "<meta http-equiv='refresh' content='5'>"
        "<title>IoT Monitor - Dashboard</title>"
        "<style>"
        "* { margin:0; padding:0; box-sizing:border-box; }"
        "body { font-family:sans-serif; background:#1a1a2e; color:#e0e0e0; padding:20px; }"
        "h1 { color:#0ea5e9; margin-bottom:4px; }"
        "p.sub { color:#888; margin-bottom:20px; font-size:14px; }"
        ".grid { display:flex; gap:16px; flex-wrap:wrap; margin-bottom:20px; }"
        ".card { background:#16213e; border-radius:10px; padding:20px; flex:1; min-width:200px; }"
        ".card h3 { color:#888; font-size:13px; margin-bottom:6px; text-transform:uppercase; }"
        ".card .val { font-size:28px; font-weight:bold; }"
        "table { width:100%%; border-collapse:collapse; margin-top:10px; }"
        "th,td { text-align:left; padding:8px 12px; border-bottom:1px solid #1e3a5f; font-size:13px; }"
        "th { color:#888; text-transform:uppercase; font-size:11px; }"
        ".topbar { display:flex; justify-content:space-between; align-items:center; margin-bottom:20px; }"
        ".logout { color:#888; text-decoration:none; font-size:13px; }"
        ".logout:hover { color:#f87171; }"
        ".badge { display:inline-block; padding:2px 8px; border-radius:4px; font-size:11px; }"
        ".badge-ok { background:#065f46; color:#4ade80; }"
        ".badge-warn { background:#78350f; color:#fbbf24; }"
        ".badge-crit { background:#7f1d1d; color:#f87171; }"
        "</style></head><body>"
        "<div class='topbar'>"
        "<div><h1>IoT Equipment Monitor</h1><p class='sub'>Dashboard — auto-refresh every 5s</p></div>"
        "<a href='/' class='logout'>Logout</a>"
        "</div>"
        "<div class='grid'>"
        "<div class='card'><h3>RPM</h3><div class='val'>%d</div></div>"
        "<div class='card'><h3>System Load</h3><div class='val' style='color:%s'>%d%%</div></div>"
        "<div class='card'><h3>Temperature</h3><div class='val' style='color:%s'>%d°C</div></div>"
        "<div class='card'><h3>Pressure</h3><div class='val' style='color:%s'>%d bar</div></div>"
        "<div class='card'><h3>Heading</h3><div class='val'>%s</div></div>"
        "<div class='card'><h3>Clients</h3><div class='val'>%d</div></div>"
        "</div>"
        "<div class='card' style='margin-bottom:20px'>"
        "<h3>Sensors (simulated fleet)</h3>"
        "<table><tr><th>Equipment ID</th><th>Type</th><th>Status</th></tr>"
        "<tr><td>PUMP-001</td><td>Pressure</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "<tr><td>MOTOR-001</td><td>RPM</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "<tr><td>COOLER-001</td><td>Temperature</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "<tr><td>VIBR-001</td><td>Vibration</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "<tr><td>ENERGY-001</td><td>Energy</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "<tr><td>HUMIDITY-001</td><td>Humidity</td><td><span class='badge badge-ok'>Active</span></td></tr>"
        "</table></div>"
        "<div class='card'>"
        "<h3>Recent alerts (%d)</h3>"
        "<table><tr><th>Severity</th><th>Type</th><th>Message</th><th>Time</th></tr>"
        "%s"
        "</table></div>"
        "</body></html>",
        snap.equipment_rpm,
        load_color, snap.system_load_percent,
        temp_color, snap.coolant_temperature,
        pres_color, snap.hydraulic_pressure,
        hdg,
        client_count,
        alert_count,
        alert_rows);

    send_html(fd, 200, "OK", NULL, html);
    #undef MAX_DASH_ALERTS
}

/* ── Route handlers ── */

static void handle_request(int fd, const char *request) {
    /* Parse request line: GET /path HTTP/1.x */
    char method[16] = {0};
    char path[512] = {0};

    if (sscanf(request, "%15s %511s", method, path) < 2) {
        send_html(fd, 400, "Bad Request", NULL,
                  "<h1>400 Bad Request</h1>");
        return;
    }

    /* Only support GET */
    if (strcmp(method, "GET") != 0) {
        send_html(fd, 405, "Method Not Allowed", NULL,
                  "<h1>405 Method Not Allowed</h1>");
        return;
    }

    logger_write(NULL, "HTTP %s %s", method, path);

    /* ── Route: / (login page) ── */
    if (strcmp(path, "/") == 0 || strcmp(path, "/index.html") == 0) {
        generate_login_page(fd, NULL);
    }
    /* ── Route: /login?user=X&pass=Y ── */
    else if (strncmp(path, "/login", 6) == 0) {
        char user[64] = {0}, pass[64] = {0};
        char *query = strchr(path, '?');

        if (query && get_param(query + 1, "user", user, sizeof(user))
                  && get_param(query + 1, "pass", pass, sizeof(pass))) {

            if (strcmp(user, "engineer") == 0 && strcmp(pass, "eng2025") == 0) {
                /* Auth success → set cookie and redirect to dashboard */
                send_html(fd, 302, "Found",
                    "Set-Cookie: iot_auth=engineer; Path=/; HttpOnly\r\n"
                    "Location: /dashboard\r\n",
                    "<p>Redirecting...</p>");
            } else {
                generate_login_page(fd, "Invalid username or password");
            }
        } else {
            generate_login_page(fd, "Please enter username and password");
        }
    }
    /* ── Route: /dashboard ── */
    else if (strcmp(path, "/dashboard") == 0) {
        if (!check_auth_cookie(request)) {
            send_html(fd, 302, "Found",
                "Location: /\r\n",
                "<p>Redirecting to login...</p>");
            return;
        }
        generate_dashboard(fd);
    }
    /* ── Route: /api/status (JSON) ── */
    else if (strcmp(path, "/api/status") == 0) {
        equipment_snapshot_t snap;
        equipment_state_read(&snap);
        char json[512];
        snprintf(json, sizeof(json),
            "{\"rpm\":%d,\"load\":%d,\"temp\":%d,\"pressure\":%d,\"heading\":\"%s\"}",
            snap.equipment_rpm, snap.system_load_percent,
            snap.coolant_temperature, snap.hydraulic_pressure,
            heading_to_string(snap.equipment_heading));
        send_json(fd, 200, json);
    }
    /* ── Route: /api/alerts (JSON) ── */
    else if (strcmp(path, "/api/alerts") == 0) {
        #define MAX_JSON_ALERTS 10
        alert_record_t alerts[MAX_JSON_ALERTS];
        int count = alert_history_copy(alerts, MAX_JSON_ALERTS);

        char json[4096];
        int off = snprintf(json, sizeof(json), "{\"count\":%d,\"alerts\":[", count);
        for (int i = 0; i < count && off < (int)sizeof(json) - 256; i++) {
            if (i > 0) off += snprintf(json + off, sizeof(json) - off, ",");
            off += snprintf(json + off, sizeof(json) - off,
                "{\"severity\":\"%s\",\"type\":\"%s\",\"message\":\"%s\",\"timestamp\":\"%s\"}",
                alert_severity_string(alerts[i].severity),
                alerts[i].type, alerts[i].message, alerts[i].timestamp);
        }
        snprintf(json + off, sizeof(json) - off, "]}");
        send_json(fd, 200, json);
        #undef MAX_JSON_ALERTS
    }
    /* ── Route: /api/sensors (JSON) ── */
    else if (strcmp(path, "/api/sensors") == 0) {
        int count = connection_registry_count();
        char json[1024];
        snprintf(json, sizeof(json),
            "{\"connected_clients\":%d,\"sensors\":["
            "{\"id\":\"PUMP-001\",\"type\":\"pressure\",\"status\":\"active\"},"
            "{\"id\":\"MOTOR-001\",\"type\":\"rpm\",\"status\":\"active\"},"
            "{\"id\":\"COOLER-001\",\"type\":\"temperature\",\"status\":\"active\"},"
            "{\"id\":\"VIBR-001\",\"type\":\"vibration\",\"status\":\"active\"},"
            "{\"id\":\"ENERGY-001\",\"type\":\"energy\",\"status\":\"active\"},"
            "{\"id\":\"HUMIDITY-001\",\"type\":\"humidity\",\"status\":\"active\"}"
            "]}", count);
        send_json(fd, 200, json);
    }
    /* ── 404 ── */
    else {
        send_html(fd, 404, "Not Found", NULL,
            "<!DOCTYPE html><html><body style='font-family:sans-serif;"
            "background:#1a1a2e;color:#e0e0e0;display:flex;justify-content:center;"
            "align-items:center;min-height:100vh'>"
            "<div><h1 style='color:#f87171'>404</h1>"
            "<p>Page not found</p></div></body></html>");
    }
}

/* ── Per-connection thread ── */

static void *http_client_thread(void *arg) {
    int fd = *(int *)arg;
    free(arg);

    char buf[4096] = {0};
    ssize_t n = recv(fd, buf, sizeof(buf) - 1, 0);
    if (n > 0) {
        buf[n] = '\0';
        handle_request(fd, buf);
    }

    close(fd);
    return NULL;
}

/* ── Accept loop ── */

static void *http_accept_loop(void *arg) {
    (void)arg;

    logger_write(NULL, "HTTP server started on fd=%d", g_http_fd);

    while (!atomic_load(&g_http_stop)) {
        struct sockaddr_in cli;
        socklen_t len = sizeof(cli);

        int cfd = accept(g_http_fd, (struct sockaddr *)&cli, &len);
        if (cfd < 0) {
            if (errno == EINTR || errno == EBADF) break;
            continue;
        }

        int *fd_ptr = malloc(sizeof(int));
        if (!fd_ptr) { close(cfd); continue; }
        *fd_ptr = cfd;

        pthread_t th;
        pthread_create(&th, NULL, http_client_thread, fd_ptr);
        pthread_detach(th);
    }

    logger_write(NULL, "HTTP server stopped");
    return NULL;
}

/* ── Public API ── */

int http_server_start(int port) {
    g_http_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (g_http_fd < 0) {
        logger_write(NULL, "HTTP: socket() failed: %s", strerror(errno));
        return -1;
    }

    int opt = 1;
    setsockopt(g_http_fd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family      = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_ANY);
    addr.sin_port        = htons((uint16_t)port);

    if (bind(g_http_fd, (struct sockaddr *)&addr, sizeof(addr)) < 0) {
        logger_write(NULL, "HTTP: bind(%d) failed: %s", port, strerror(errno));
        close(g_http_fd);
        g_http_fd = -1;
        return -1;
    }

    if (listen(g_http_fd, 16) < 0) {
        logger_write(NULL, "HTTP: listen() failed: %s", strerror(errno));
        close(g_http_fd);
        g_http_fd = -1;
        return -1;
    }

    atomic_store(&g_http_stop, 0);
    pthread_create(&g_http_tid, NULL, http_accept_loop, NULL);

    logger_write(NULL, "HTTP server listening on port %d", port);
    return 0;
}

void http_server_stop(void) {
    atomic_store(&g_http_stop, 1);
    if (g_http_fd >= 0) {
        shutdown(g_http_fd, SHUT_RDWR);
        close(g_http_fd);
        g_http_fd = -1;
    }
    pthread_join(g_http_tid, NULL);
}