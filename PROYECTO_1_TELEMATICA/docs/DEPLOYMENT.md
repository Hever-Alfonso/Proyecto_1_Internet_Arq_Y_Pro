# Deployment guide

## Prerequisites

| Component | Version | Purpose |
|-----------|---------|---------|
| GCC / Clang | 9+ | Compile server |
| Make | any | Build system |
| Go | 1.21+ | Sensor client |
| Python | 3.10+ | Operator client |
| Docker | 20+ | Containerisation |
| Docker Compose | 2+ | Service orchestration |
| AWS account | — | Cloud deployment |

---

## 1. Local development

### 1.1 Clone the repository

```bash
git clone https://github.com/Hever-Alfonso/Proyecto_1_Internet_Arq_Y_Pro.git
cd Proyecto_1_Internet_Arq_Y_Pro/PROYECTO_1_TELEMATICA
```

### 1.2 Compile and run the server

```bash
cd server
make clean && make
./server 9000 server.log
```

The server starts two services:
- **TCP protocol** on port 9000
- **HTTP web dashboard** on port 9080 (port + 80)

Expected output:

```
[...] - AUTH_SERVICE: loaded 'users.conf' (3 users)
[...] - HTTP server listening on port 9080
[...] - Server listening on port 9000 (Ctrl+C to stop)
[...] - HTTP dashboard on http://localhost:9080
[...] - metrics_processor started (interval=5s)
```

### 1.3 Run the sensor client (Go)

Open a new terminal:

```bash
cd clients/sensor_client
go run . localhost 9000
```

Expected output:

```
═══════════════════════════════════════════
  IoT Sensor Client (Go)
═══════════════════════════════════════════
  Server:   localhost:9000
  Sensors:  6 devices
  Interval: 5s
═══════════════════════════════════════════

Registered 6 sensors:
  1. PUMP-001        type=pressure     unit=bar
  2. MOTOR-001       type=rpm          unit=rev/min
  3. COOLER-001      type=temperature  unit=C
  4. VIBR-001        type=vibration    unit=mm/s
  5. ENERGY-001      type=energy       unit=kW
  6. HUMIDITY-001    type=humidity     unit=percent
```

### 1.4 Run the operator client (Python)

Open a new terminal:

```bash
cd clients/operator_client
pip install -r requirements.txt
python3 main.py localhost 9000
```

Click **CONNECT** in the GUI to start receiving data.

### 1.5 Open the web dashboard

Open a browser and go to:

```
http://localhost:9080
```

Login: `engineer` / `eng2025`

After login you will see the dashboard with:
- Equipment metrics (RPM, load, temperature, pressure, heading)
- Sensor fleet table (6 sensors, all Active)
- Recent alerts table
- Auto-refresh every 5 seconds

Other URLs available:
- `http://localhost:9080/api/status` — JSON equipment status
- `http://localhost:9080/api/sensors` — JSON sensor fleet
- `http://localhost:9080/api/alerts` — JSON recent alerts

### 1.6 Test with netcat

Quick manual test without clients:

```bash
nc localhost 9000
```

Then type:

```
HELLO name=test
AUTHENTICATE engineer eng2025
GET_STATUS
MODIFY_RPM 500
ADJUST_HEADING RIGHT
GET_ALERTS
QUIT
```

---

## 2. Docker deployment (local)

### 2.1 Build the server image

```bash
cd server
docker build -t iot-server:1.0 .
```

Verify the image:

```bash
docker images | grep iot-server
```

### 2.2 Run with docker

```bash
docker run -d \
  --name iot-server \
  -p 9000:9000 \
  -p 9080:9080 \
  -v $(pwd)/logs:/app/logs \
  -v $(pwd)/users.conf:/app/users.conf \
  iot-server:1.0
```

Check it is running:

```bash
docker ps
docker logs -f iot-server
```

### 2.3 Run with docker compose

From the project root (`PROYECTO_1_TELEMATICA/`):

```bash
docker compose up -d
```

View logs:

```bash
docker compose logs -f
```

Stop:

```bash
docker compose down
```

### 2.4 Connect clients to Docker

Clients connect the same way — the server is exposed on ports 9000 and 9080:

```bash
# Sensor client
cd clients/sensor_client
go run . localhost 9000

# Operator client
cd clients/operator_client
python3 main.py localhost 9000

# Web dashboard
# Open http://localhost:9080 in browser
```

---

## 3. AWS EC2 deployment — step by step

### PART 1: Create an AWS account (if you don't have one)

**Step 1.1:** Go to `https://aws.amazon.com` and click **"Create an AWS Account"**

**Step 1.2:** Fill in your email, password, and account name

**Step 1.3:** Enter a credit/debit card — **you will not be charged** if you use the Free Tier (t2.micro is free for 12 months)

**Step 1.4:** Complete phone/SMS verification

**Step 1.5:** Select **"Basic Support — Free"** and wait a few minutes for the account to activate

---

### PART 2: Create an EC2 instance

**Step 2.1:** Go to `https://console.aws.amazon.com` and sign in

**Step 2.2:** Search for **"EC2"** in the top search bar and click it

**Step 2.3 — Select region (IMPORTANT):** In the top-right corner select the closest region. For Colombia: **US East (N. Virginia)** or **South America (São Paulo)**

**Step 2.4:** Click the orange **"Launch instance"** button

**Step 2.5 — Configure the instance:**

**Name:**
```
proyecto-telematica
```

**OS:** Click **"Ubuntu"** → **"Ubuntu Server 22.04 LTS"** → Architecture: **64-bit (x86)**

**Instance type:** Select **"t2.micro"** (shows "Free tier eligible")

**Key pair — VERY IMPORTANT:**
- Click **"Create new key pair"**
- Key pair name: `iot-key` — Type: **RSA** — Format: **.pem**
- Click **"Create key pair"** — file `iot-key.pem` will download
- **Save this file in a safe place. Without it you cannot connect to the server.**

**Network settings → click "Edit" and add 3 rules:**

| Type | Port | Source | Description |
|------|------|--------|-------------|
| SSH | 22 | Anywhere (0.0.0.0/0) | Remote access |
| Custom TCP | 9000 | Anywhere (0.0.0.0/0) | IoT Protocol |
| Custom TCP | 9080 | Anywhere (0.0.0.0/0) | IoT Web Dashboard |

**Storage:** leave at **8 GiB**

**Step 2.6:** Click **"Launch instance"**

**Step 2.7:** Click on the instance ID link → find **"Public IPv4 address"** → **note this IP** (e.g. `54.123.45.67`). Wait until "Instance state" shows **"Running"** (green dot).

---

### PART 3: Connect to the instance

**Step 3.1:** Open a terminal on your local machine (Mac: Cmd + Space → "Terminal")

**Step 3.2:** Navigate to where the key is:
```bash
cd ~/Downloads
```

**Step 3.3:** Give permissions to the key:
```bash
chmod 400 iot-key.pem
```

**Step 3.4:** Connect via SSH (replace `TU_IP` with your IP from step 2.7):
```bash
ssh -i iot-key.pem ubuntu@TU_IP
```

The first time it will ask:
```
Are you sure you want to continue connecting (yes/no)?
```
Type **yes** and press Enter. You are now inside the Amazon server:
```
ubuntu@ip-172-31-XX-XX:~$
```

If you get **"Permission denied"**: verify you ran `chmod 400` and the filename is correct.
If you get **"Connection timed out"**: verify the Security Group has port 22 open and the IP is correct.

---

### PART 4: Install software on EC2

Inside the SSH session, run these commands one by one:

**Step 4.1:** Update the system:
```bash
sudo apt update && sudo apt upgrade -y
```

**Step 4.2:** Install Docker:
```bash
sudo apt install -y docker.io docker-compose-plugin
```

**Step 4.3:** Add your user to the docker group:
```bash
sudo usermod -aG docker ubuntu
```

**Step 4.4:** Install Git:
```bash
sudo apt install -y git
```

**Step 4.5:** Disconnect and reconnect:
```bash
exit
```
```bash
ssh -i iot-key.pem ubuntu@TU_IP
```

**Step 4.6:** Verify Docker:
```bash
docker --version
docker compose version
```

---

### PART 5: Upload the project

**Option A — Clone from GitHub (recommended):**
```bash
git clone https://github.com/Hever-Alfonso/Proyecto_1_Internet_Arq_Y_Pro.git
cd Proyecto_1_Internet_Arq_Y_Pro/PROYECTO_1_TELEMATICA
```

**Option B — Upload with SCP (if GitHub is not available):**

From your local machine (NOT from SSH), open another terminal:
```bash
scp -i ~/Downloads/telematica-key.pem -r \
  ~/Desktop/Internet_Arquitectura_Protocolos/Proyecto_1_Internet_Arq_Y_Pro/PROYECTO_1_TELEMATICA \
  ubuntu@TU_IP:~/
```

Then on the SSH session:
```bash
cd ~/PROYECTO_1_TELEMATICA
```

Verify the project is there:
```bash
ls
```
You should see: `clients  docker-compose.yml  docs  README.md  server`

---

### PART 6: Deploy with Docker

**Step 6.1:** Build and run:
```bash
docker compose up -d --build
```

The first time takes 2–5 minutes. You can check logs with:
```bash
docker logs -f iot-server
```

Expected output:
```
[...] - AUTH_SERVICE: loaded 'users.conf' (3 users)
[...] - HTTP server listening on port 9080
[...] - Server listening on port 9000 (Ctrl+C to stop)
[...] - HTTP dashboard on http://localhost:9080
[...] - metrics_processor started (interval=5s)
```

**Step 6.2:** Verify it is running:
```bash
docker ps
```

You should see the `iot-server` container with status "Up" and ports `0.0.0.0:9000->9000/tcp, 0.0.0.0:9080->9080/tcp`.

Press **Ctrl+C** to exit logs (server keeps running).

---

### PART 7: Test from the Internet

Replace `TU_IP` with your EC2 public IPv4 address in all commands below.

**Step 7.1 — Web dashboard (browser):**

From your computer (NOT from SSH), open the browser and go to:
```
http://TU_IP:9080
```
You should see the IoT Monitor login page.
Login: `engineer` / `eng2026`

After login you will see the dashboard with real-time metrics, sensors, and alerts.

**Step 7.2 — JSON API (browser):**
```
http://TU_IP:9080/api/status
http://TU_IP:9080/api/sensors
http://TU_IP:9080/api/alerts
```

**Step 7.3 — TCP protocol (netcat):**
```bash
nc TU_IP 9000
```
Type:
```
HELLO name=RemoteTest
AUTHENTICATE engineer eng2025
GET_STATUS
QUIT
```

**Step 7.4 — Connect Go sensors from your laptop:**
```bash
cd clients/sensor_client
go run . TU_IP 9000
```

**Step 7.5 — Connect Python operator from your laptop:**
```bash
cd clients/operator_client
python3 main.py TU_IP 9000
```
Click **CONNECT** in the GUI.

---

### PART 8: Configure DNS with Route 53 (optional)

The project code uses DNS resolution (`getaddrinfo` in Python, `net.Dial` in Go) — no hardcoded IPs in the logic. Clients accept hostnames as arguments.

This allows access via a hostname like `iot.yourdomain.com` instead of the IP.

1. In the AWS Console, search for **"Route 53"**
2. Click **"Create hosted zone"** — enter your domain, select **Public hosted zone**
3. Click **"Create record"** — type: **A**, name: `iot`, value: your EC2 public IP, TTL: 300
4. Wait 5 minutes to 24 hours for DNS propagation
5. Test: `nc iot.yourdomain.com 9000` / `http://iot.yourdomain.com:9080`

**Note:** If you don't have a domain, skip this section. The public IP works fine for the presentation. Just mention that "Route 53 can be configured for a custom hostname" and show that the code supports hostnames (no hardcoded IPs).

---

## 4. Useful EC2 commands

```bash
# Check container is running
docker ps

# View logs in real time
docker logs -f iot-server

# View last 50 log lines
docker logs iot-server | tail -50

# Restart the server
docker compose restart

# Stop the server
docker compose down

# Rebuild after changes
docker compose down
docker compose up -d --build

# Monitor resource usage
docker stats

# Enter the container
docker exec -it iot-server bash

# View log file inside container
docker exec iot-server cat /app/logs/server.log

# View users file inside container
docker exec iot-server cat /app/users.conf
```

---

## 5. Maintenance commands

### View server logs

```bash
# Docker
docker logs -f iot-server

# Log file on host
tail -f logs/server.log
```

### Update deployment

```bash
cd Proyecto_1_Internet_Arq_Y_Pro/PROYECTO_1_TELEMATICA
git pull
docker compose down
docker compose build
docker compose up -d
```

---

## 6. Presentation checklist

What you should be able to demonstrate:

**1. Build the Docker image:**
```bash
ssh -i iot-key.pem ubuntu@TU_IP
cd Proyecto_1_Internet_Arq_Y_Pro/PROYECTO_1_TELEMATICA
docker compose build
```
Show it compiles without errors.

**2. Run the container on AWS:**
```bash
docker compose up -d
docker ps
docker logs iot-server
```
Show it is running with all modules (AUTH_SERVICE, HTTP server, metrics_processor).

**3. Web access from Internet:**
- Open `http://TU_IP:9080` in browser → show login page
- Login with `engineer` / `eng2026` → show dashboard
- Show metrics updating every 5 seconds
- Show sensor fleet table (6 sensors Active)
- Show alerts appearing when RPM is high
- Show JSON API: `/api/status`, `/api/sensors`, `/api/alerts`

**4. Sensor client from local machine:**
```bash
go run . TU_IP 9000
```
Show 6 sensors sending data and receiving `OK sensor_received`.

**5. Operator client from local machine:**
```bash
python3 main.py TU_IP 9000
```
Click CONNECT, show metrics updating, use RPM buttons, show alerts appearing.

**6. Multiple simultaneous clients:**
- Have Go sensors + Python operator + web dashboard all connected at the same time
- Show the web dashboard displays the correct client count

**7. Protocol test with netcat:**
```bash
nc TU_IP 9000
AUTHENTICATE engineer eng2026
GET_STATUS
MODIFY_RPM 500
GET_ALERTS
LIST USERS
QUIT
```

**8. Show logs:**
```bash
docker logs iot-server | tail -50
```
Show timestamps, client IPs, requests, responses, SENSOR_DATA entries, METRIC broadcasts, ALERT detections.

**9. Show external authentication:**
```bash
docker exec iot-server cat /app/users.conf
```
Show that credentials are stored externally, not hardcoded in the server binary.

---

## 7. Troubleshooting

### Server won't start

```bash
# Check if port is in use
lsof -i :9000

# Kill the process using it (Linux)
kill -9 $(lsof -t -i :9000)

# Kill the process using it (macOS)
lsof -ti :9000 | xargs kill -9
```

### Cannot connect from outside (AWS)

```bash
# Verify security group in AWS Console has ports 9000 and 9080 open
# Test port reachability
nc -zv <PUBLIC_IP> 9000
nc -zv <PUBLIC_IP> 9080
```

### "Connection timed out" on SSH

- Verify the instance is **Running** (not Stopped)
- Verify port 22 is open in the Security Group
- Verify the IP is correct (it can change if the instance was restarted)

### "Permission denied" on SSH

```bash
chmod 400 iot-key.pem
```
Make sure you use `ubuntu@` before the IP (not `root@`).

### Docker daemon not running

```bash
sudo systemctl start docker
sudo systemctl enable docker
sudo systemctl status docker
```

### Permission denied on Docker

```bash
sudo usermod -aG docker $USER
# Then logout and login again
```

### Docker build fails on EC2

```bash
sudo apt update
sudo apt install -y docker.io docker-compose-plugin
sudo usermod -aG docker ubuntu
exit
# Reconnect SSH and retry
```

### Port 9080 not reachable from browser

- Verify Security Group has a rule for port 9080
- Verify `docker-compose.yml` has `"9080:9080"` in ports
- Restart: `docker compose down && docker compose up -d`

### Sensor client can't connect

```bash
# Verify server is running
docker ps

# Check server logs for errors
docker logs iot-server

# Test port manually
nc -zv <HOST> 9000
```

### "Cannot connect" from clients to AWS

- Use the **public IP** (not the private one — 172.x.x.x does not work from outside AWS)
- The public IP is shown in EC2 console as "Public IPv4 address"

### Instance shut down by itself

AWS may stop inactive Free Tier instances. Just restart it from the EC2 console. **The public IP may change after restarting — check the new IP.**

### macOS compilation issues

```bash
# Install Xcode command line tools
xcode-select --install

# If binary is quarantined
xattr -d com.apple.quarantine server
```

---

## 8. Costs

| Resource | Cost |
|----------|------|
| EC2 t2.micro | Free tier (first 12 months) |
| EBS storage 8 GB | Free tier |
| Data transfer | Free (up to 100 GB/month) |
| Route 53 (if used) | $0.50/month per hosted zone |
| **Total without Route 53** | **$0** |

**Stop the instance when not in use:**
1. Go to EC2 in the AWS Console
2. Select your instance → **"Instance state" → "Stop instance"**
3. To restart: **"Instance state" → "Start instance"** (verify the new public IP)
4. To delete everything at the end of the course: **"Instance state" → "Terminate instance"**

---

## 9. Quick reference

| Action | Command |
|--------|---------|
| Build server locally | `cd server && make clean && make` |
| Run server locally | `./server 9000 server.log` |
| Run sensor client | `cd clients/sensor_client && go run . <host> <port>` |
| Run operator client | `cd clients/operator_client && python3 main.py <host> <port>` |
| Open web dashboard | `http://<host>:9080` in browser |
| Docker build | `docker build -t iot-server:1.0 server/` |
| Docker run (compose) | `docker compose up -d` |
| Docker stop | `docker compose down` |
| Docker logs | `docker compose logs -f` |
| SSH to EC2 | `ssh -i iot-key.pem ubuntu@<PUBLIC_IP>` |
| Test TCP connection | `nc <host> 9000` |
| Test web interface | `http://<host>:9080` |
| JSON API status | `http://<host>:9080/api/status` |
| JSON API sensors | `http://<host>:9080/api/sensors` |
| JSON API alerts | `http://<host>:9080/api/alerts` |
| Clone repo on EC2 | `git clone https://github.com/Hever-Alfonso/Proyecto_1_Internet_Arq_Y_Pro.git` |
