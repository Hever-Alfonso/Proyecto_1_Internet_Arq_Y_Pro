# Deployment guide

## Prerequisites

| Component | Version | Purpose |
|-----------|---------|---------|
| GCC | 9+ | Compile server |
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
cd Proyecto_1_Internet_Arq_Y_Pro
```

### 1.2 Compile and run the server

```bash
cd server
make clean && make
./server 9000 server.log
```

Expected output:

```
[2026-03-30 14:00:00] - Server listening on port 9000 (Ctrl+C to stop)
[2026-03-30 14:00:00] - metrics_processor started (interval=5s)
```

### 1.3 Run the sensor client (Go)

Open a new terminal:

```bash
cd clients/sensor_client
go run . 127.0.0.1 9000
```

Expected output:

```
═══════════════════════════════════════════
  IoT Sensor Client (Go)
═══════════════════════════════════════════
  Server:   127.0.0.1:9000
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
python main.py 127.0.0.1 9000
```

Click **CONNECT** in the GUI to start receiving data.

### 1.5 Test with netcat

Quick manual test without clients:

```bash
nc localhost 9000
```

Then type:

```
HELLO name=test
AUTHENTICATE engineer eng2026
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
  -v $(pwd)/logs:/app/logs \
  iot-server:1.0
```

Check it is running:

```bash
docker ps
docker logs -f iot-server
```

### 2.3 Run with docker-compose

From the project root:

```bash
docker-compose up -d
```

View logs:

```bash
docker-compose logs -f
```

Stop:

```bash
docker-compose down
```

### 2.4 Connect clients to Docker

Clients connect the same way — the server is exposed on port 9000:

```bash
# Sensor client
cd clients/sensor_client
go run . 127.0.0.1 9000

# Operator client
cd clients/operator_client
python main.py 127.0.0.1 9000
```

---

## 3. AWS EC2 deployment

### 3.1 Launch an EC2 instance

1. Open **AWS Console → EC2 → Launch Instance**
2. Select **Ubuntu 22.04 LTS** (t2.micro is sufficient)
3. Create or select a **Key Pair** (download the .pem file)
4. Configure **Security Group**:

| Type | Port | Source | Purpose |
|------|------|--------|---------|
| SSH | 22 | Your IP | Remote access |
| Custom TCP | 9000 | 0.0.0.0/0 | TCP protocol (clients) |
| Custom TCP | 9080 | 0.0.0.0/0 | HTTP dashboard (web) |

5. Launch the instance
6. Note the **Public IP address**

### 3.2 Connect via SSH

```bash
chmod 400 your-key.pem
ssh -i your-key.pem ubuntu@<PUBLIC_IP>
```

### 3.3 Install Docker on EC2

```bash
sudo apt update && sudo apt upgrade -y
sudo apt install -y docker.io docker-compose
sudo usermod -aG docker ubuntu
exit
```

Reconnect for group changes to take effect:

```bash
ssh -i your-key.pem ubuntu@<PUBLIC_IP>
```

Verify Docker:

```bash
docker --version
```

### 3.4 Deploy the application

```bash
git clone https://github.com/Hever-Alfonso/Proyecto_1_Internet_Arq_Y_Pro.git
cd Proyecto_1_Internet_Arq_Y_Pro
docker-compose up -d
```

Verify:

```bash
docker ps
docker logs -f iot-server
```

### 3.5 Connect from your local machine

Replace `<PUBLIC_IP>` with your EC2 public IP:

```bash
# Test with netcat
nc <PUBLIC_IP> 9000

# Sensor client
cd clients/sensor_client
go run . <PUBLIC_IP> 9000

# Operator client
cd clients/operator_client
python main.py <PUBLIC_IP> 9000
```

### 3.6 Verify from browser (optional health check)

```bash
curl http://<PUBLIC_IP>:9080
```

The HTTP dashboard will respond with the login page, confirming the web interface is reachable.

---

## 4. Maintenance commands

### View server logs

```bash
# Docker
docker logs -f iot-server

# Log file on host
tail -f logs/server.log
```

### Restart the server

```bash
docker-compose restart
```

### Update deployment

```bash
cd iot-monitoring-system
git pull
docker-compose down
docker-compose build
docker-compose up -d
```

### Monitor resources

```bash
docker stats
docker exec iot-server ps aux
```

---

## 5. Troubleshooting

### Server won't start

```bash
# Check if port is in use
lsof -i :9000

# Kill the process using it
kill -9 <PID>

# Or change the port in docker-compose.yml
```

### Cannot connect from outside

```bash
# Verify security group in AWS Console
# Ensure port 9000 is open for inbound TCP from 0.0.0.0/0

# Test port reachability
nc -zv <PUBLIC_IP> 9000
```

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

### Sensor client can't connect

```bash
# Verify server is running
docker ps

# Check server logs for errors
docker logs iot-server

# Test port manually
nc -zv <HOST> 9000
```

---

## 6. Costs

| Resource | Cost |
|----------|------|
| EC2 t2.micro | Free tier (first 12 months) |
| Data transfer | Minimal (text protocol, small payloads) |
| EBS storage | 8 GB free tier |
| **Total** | **$0 (free tier)** |

---

## 7. Quick reference

| Action | Command |
|--------|---------|
| Build server | `cd server && make` |
| Run server locally | `./server 9000 server.log` |
| Run sensor client | `cd clients/sensor_client && go run . <host> <port>` |
| Run operator client | `cd clients/operator_client && python main.py <host> <port>` |
| Docker build | `docker build -t iot-server:1.0 server/` |
| Docker run | `docker-compose up -d` |
| Docker stop | `docker-compose down` |
| Docker logs | `docker-compose logs -f` |
| SSH to EC2 | `ssh -i key.pem ubuntu@<IP>` |
| Test connection | `nc <host> 9000` |