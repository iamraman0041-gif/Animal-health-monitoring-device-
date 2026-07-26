#Generalized code for fifo

#include <WiFi.h>
#include <WebServer.h>
#include <Wire.h>
#include <OneWire.h>
#include <DallasTemperature.h>

/* ================= GPIO ================= */
#define MPU_SDA 19
#define MPU_SCL 18
#define ONE_WIRE_BUS 5
#define BATTERY_PIN 34 // ADC

/* ================= OBJECTS ================= */
OneWire oneWire(ONE_WIRE_BUS);
DallasTemperature sensors(&oneWire);
WebServer server(80);

/* ================= AP MODE ================= */
const char* ssid = "Cow_Health_Monitor";
const char* password = "12345678";

/* ================= VARIABLES ================= */
float tempC = 0, tempF = 0;
float batteryVoltage = 0;
int16_t ax, ay, az;
int16_t prevAx = 0, prevAy = 0, prevAz = 0;
int activity = 0;
String health = "HEALTHY";

/* ================= FUNCTIONS ================= */
String webpage();
void readMPU();
void handleRoot();
void handleData();

/* ================= SETUP ================= */
void setup() {
    Serial.begin(115200);

    // Start WiFi AP
    WiFi.softAP(ssid, password);
    Serial.println(WiFi.softAPIP());

    // Initialize I2C for MPU6050
    Wire.begin(MPU_SDA, MPU_SCL);
    Wire.beginTransmission(0x68);
    Wire.write(0x6B); // Wake up MPU6050
    Wire.write(0);
    Wire.endTransmission(true);

    // Initialize temperature sensor
    sensors.begin();

    // Initial MPU read to set previous values
    readMPU();
    prevAx = ax;
    prevAy = ay;
    prevAz = az;

    // Web server routes
    server.on("/", handleRoot);
    server.on("/data", handleData);
    server.begin();
}

/* ================= LOOP ================= */
void loop() {
    // Read temperature
    sensors.requestTemperatures();
    tempC = sensors.getTempCByIndex(0);
    tempF = (tempC * 9.0 / 5.0) + 32.0;

    // Read MPU6050
    readMPU();

    // Orientation-independent activity calculation
    int delta = abs(ax - prevAx) + abs(ay - prevAy) + abs(az - prevAz);
    activity = delta / 50; // scale down

    // Noise filter
    if (activity < 10) activity = 0;

    // Save current MPU values for next iteration
    prevAx = ax;
    prevAy = ay;
    prevAz = az;

    // Battery voltage reading
    int adc = analogRead(BATTERY_PIN);
    float v = (adc / 4095.0) * 3.3;
    batteryVoltage = v * 2.0; // Voltage divider

    // Health logic
    if (tempC >= 40.0 || activity > 250) {
        health = "SICK / HYPER";
    } else if (activity == 0) {
        health = "INACTIVE";
    } else {
        health = "HEALTHY";
    }

    // Handle web server client requests
    server.handleClient();

    delay(1000);
}

/* ================= MPU READ ================= */
void readMPU() {
    Wire.beginTransmission(0x68);
    Wire.write(0x3B);
    Wire.endTransmission(false);
    Wire.requestFrom(0x68, 6, true);

    ax = Wire.read() << 8 | Wire.read();
    ay = Wire.read() << 8 | Wire.read();
    az = Wire.read() << 8 | Wire.read();
}

/* ================= JSON ================= */
void handleData() {
    String json = "{";
    json += "\"tempC\":" + String(tempC, 1) + ",";
    json += "\"tempF\":" + String(tempF, 1) + ",";
    json += "\"activity\":" + String(activity) + ",";
    json += "\"battery\":" + String(batteryVoltage, 2) + ",";
    json += "\"health\":\"" + health + "\"";
    json += "}";

    server.send(200, "application/json", json);
}

/* ================= ROOT ================= */
void handleRoot() {
    server.send(200, "text/html", webpage());
}

/* ================= WEB PAGE ================= */
String webpage() {
    return R"rawliteral(
<!DOCTYPE html>
<html>
<head>
<meta charset="UTF-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>Cow Health Monitor</title>
<style>
body { font-family: Arial; text-align:center; background:#eef2f5; transition:0.5s; }
.card { background:#fff; margin:12px; padding:15px; border-radius:12px; box-shadow:0 0 10px rgba(0,0,0,0.15); }
.green { color:green; font-weight:bold; }
.red { color:red; font-weight:bold; }
.yellow { color:orange; font-weight:bold; }
.graph-card { background:#ffffff; margin:12px; padding:10px; border-radius:12px; box-shadow:0 0 10px rgba(0,0,0,0.15); }
.redbg { background:#ffd6d6; animation: blink 1s infinite; }
@keyframes blink { 50% { background:#ffffff; } }
</style>
</head>
<body id="body">
<h2>🐄 Cow Health Dashboard</h2>
<div class="card">
<p><b>Cow ID:</b> COW-01</p>
<p>🌡 Temp: <span id="tc"></span> °C / <span id="tf"></span> °F</p>
<p>🏃 Activity: <span id="act"></span></p>
<p>🔋 Battery: <span id="bat"></span> V</p>
<p>🕒 Time: <span id="time"></span></p>
<p>📅 Date: <span id="date"></span></p>
<p>Status: <span id="health"></span></p>
</div>
<div class="graph-card">
<h4>📊 Activity Trend</h4>
<canvas id="graph" width="320" height="160"></canvas>
</div>
<audio id="alarm">
<source src="https://actions.google.com/sounds/v1/alarms/beep_short.ogg">
</audio>
<script>
let ctx = document.getElementById("graph").getContext("2d");
let values = [];
function drawGraph(){
    ctx.clearRect(0,0,320,160);
    ctx.beginPath();
    ctx.strokeStyle = "red";
    ctx.lineWidth = 2;
    values.forEach((v,i)=>{
        ctx.lineTo(i*10, 160 - Math.min(v,150));
    });
    ctx.stroke();
}
setInterval(()=>{
    fetch("/data").then(r=>r.json()).then(j=>{
        tc.innerHTML = j.tempC;
        tf.innerHTML = j.tempF;
        act.innerHTML = j.activity;
        bat.innerHTML = j.battery.toFixed(2);

        let h = document.getElementById("health");
        if(j.health.includes("SICK") || j.health.includes("HYPER")){
            h.innerHTML = "🔴 " + j.health;
            h.className = "red";
            body.className = "redbg";
            document.getElementById("alarm").play();
        } else if(j.health.includes("INACTIVE")){
            h.innerHTML = "🟡 INACTIVE / NO MOVEMENT";
            h.className = "yellow";
            body.className = "";
        } else {
            h.innerHTML = "🟡 HEALTHY";
            h.className = "green";
            body.className = "";
        }

        values.push(j.activity);
        if(values.length > 30) values.shift();
        drawGraph();

        let d = new Date();
        time.innerHTML = d.toLocaleTimeString();
        date.innerHTML = d.toLocaleDateString();
    });
},1000);
</script>
</body>
</html>
)rawliteral";
}
