/**
 * 卫生间占用指示器 — sensorSwitch
 *
 * 硬件: ESP32(CH9102/30Pin) + LD2410C + SH1106 OLED 1.3寸
 * 供电: USB-C 5V
 * 组装: 面包板 + 杜邦线，无需焊接
 *
 * 功能:
 *   - LD2410 人体存在检测（含静止/呼吸）
 *   - OLED "空闲(笑脸)" / "使用中(心跳条)"
 *   - http://bathroom.local 查看状态
 *   - GET /api/status 返回 JSON
 */

// ===================== 依赖 =====================
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <ESPmDNS.h>
#include <ld2410.h>
#include <U8g2lib.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

// ===================== 用户配置 =====================
const char* WIFI_SSID     = "董班班的phone";
const char* WIFI_PASSWORD = "11111111";
const char* HOSTNAME      = "bathroom";

// 引脚
#define LD2410_RX  16
#define LD2410_TX  17
#define OLED_SDA    21
#define OLED_SCL    22

// 时间
#define PRESENCE_DEBOUNCE_MS  1500
#define ABSENCE_DEBOUNCE_MS   3000
#define DISPLAY_UPDATE_MS      300

// LD2410 检测范围
#define MAX_MOVING_GATE     4
#define MAX_STATIONARY_GATE 3
#define LD2410_TIMEOUT_SEC  3

// ===================== 全局对象 =====================
ld2410 radar;
U8G2_SH1106_128X64_NONAME_F_HW_I2C u8g2(U8G2_R0, U8X8_PIN_NONE, OLED_SCL, OLED_SDA);
WebServer server(80);

// ===================== 状态变量 =====================
enum State { IDLE, OCCUPIED };
State        currentState     = IDLE;
unsigned long presenceStartMs = 0;
unsigned long absenceStartMs  = 0;
unsigned long occupiedSinceMs = 0;
unsigned long lastDisplayMs   = 0;
bool         mdnsStarted      = false;

bool     moving   = false;
bool     still    = false;
uint16_t mvDist   = 0;
uint8_t  mvEnergy = 0;
uint16_t stDist   = 0;
uint8_t  stEnergy = 0;
uint16_t detDist  = 0;

// ===================== OLED 显示 =====================
void drawScreen() {
    u8g2.clearBuffer();

    if (currentState == OCCUPIED) {
        // 标题
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(40, 12, "BATHROOM");
        u8g2.drawHLine(0, 15, 128);

        u8g2.setFont(u8g2_font_9x18B_tr);
        u8g2.drawStr(46, 38, "BUSY");

        // 心跳节奏条
        unsigned long phase = millis() % 1000;
        float beat = 0;
        if (phase < 100) {
            beat = sinf(phase * PI / 100.0f);
        } else if (phase < 220) {
            beat = sinf((phase - 120) * PI / 100.0f);
        }
        int energy = still ? stEnergy : mvEnergy;
        float intensity = 0.6f + energy / 250.0f;
        beat *= intensity;

        int barW = 6 + (int)(beat * 90);
        int barX = (128 - barW) / 2;
        u8g2.drawFrame(18, 54, 92, 7);
        if (barW > 4) u8g2.drawBox(barX + 1, 56, barW - 2, 3);
    } else {
        u8g2.setFont(u8g2_font_6x12_tr);
        u8g2.drawStr(40, 12, "BATHROOM");
        u8g2.drawHLine(0, 15, 128);

        u8g2.setFont(u8g2_font_9x18B_tr);
        u8g2.drawStr(46, 38, "FREE");

        int cx = 64, cy = 52;
        u8g2.drawCircle(cx, cy, 8);
        u8g2.drawDisc(cx - 3, cy - 2, 2);
        u8g2.drawDisc(cx + 3, cy - 2, 2);
        u8g2.drawLine(cx - 3, cy + 2, cx - 1, cy + 4);
        u8g2.drawLine(cx - 1, cy + 4, cx + 1, cy + 4);
        u8g2.drawLine(cx + 1, cy + 4, cx + 3, cy + 2);
    }
    u8g2.sendBuffer();
}

// ===================== 状态机 =====================
void updateState(bool presence) {
    unsigned long now = millis();
    if (presence) {
        absenceStartMs = 0;
        if (currentState == IDLE) {
            if (!presenceStartMs) presenceStartMs = now;
            if (now - presenceStartMs >= PRESENCE_DEBOUNCE_MS) {
                currentState    = OCCUPIED;
                occupiedSinceMs = now;
                Serial.println("[STATE] -> 使用中");
            }
        }
    } else {
        presenceStartMs = 0;
        if (currentState == OCCUPIED) {
            if (!absenceStartMs) absenceStartMs = now;
            if (now - absenceStartMs >= ABSENCE_DEBOUNCE_MS) {
                currentState = IDLE;
                Serial.println("[STATE] -> 空闲");
            }
        }
    }
}

// ===================== WiFi =====================
void connectWiFi() {
    WiFi.mode(WIFI_STA);
    WiFi.setHostname(HOSTNAME);
    WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
    Serial.print("[WiFi] 连接中");
    for (int i = 0; i < 40 && WiFi.status() != WL_CONNECTED; i++) {
        delay(500); Serial.print(".");
    }
    if (WiFi.status() == WL_CONNECTED) {
        Serial.printf("\n[WiFi] 已连接  IP: %s\n", WiFi.localIP().toString().c_str());
        if (MDNS.begin(HOSTNAME)) {
            Serial.printf("[mDNS] http://%s.local\n", HOSTNAME);
            MDNS.addService("http", "tcp", 80);
            mdnsStarted = true;
        }
    } else {
        Serial.println("\n[WiFi] 失败, 仅本地显示可用");
    }
}

// ===================== Web 服务 =====================
static const char HTML_HEAD[] PROGMEM = R"html(
<!DOCTYPE html><html lang="zh"><head>
<meta charset="UTF-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<meta http-equiv="refresh" content="5">
<style>
*{margin:0;padding:0;box-sizing:border-box}
body{font-family:-apple-system,system-ui,sans-serif;display:flex;justify-content:center;
align-items:center;min-height:100vh;background:#1a1a2e;color:#eee}
.card{text-align:center;padding:40px 30px;border-radius:20px;max-width:320px;width:90%}
.occ{background:linear-gradient(135deg,#2d1f1f,#3d1a1a);border:2px solid #e74c3c}
.idl{background:linear-gradient(135deg,#1f2d1f,#1a3a1a);border:2px solid #27ae60}
.emoji{font-size:4rem;margin-bottom:8px}
h1{font-size:1.1rem;color:#aaa;font-weight:400}
.status{font-size:2.8rem;margin:16px 0;font-weight:700}
.red{color:#e74c3c}.grn{color:#27ae60}
.timer{font-size:2rem;font-variant-numeric:tabular-nums;color:#ccc;margin:12px 0}
.sig{display:inline-block;height:8px;border-radius:4px;transition:width .5s}
.hi{background:#27ae60}.md{background:#f39c12}.lo{background:#e74c3c}
.info{font-size:.8rem;color:#666;margin-top:16px}
</style></head>)html";

void handleRoot() {
    bool occ = (currentState == OCCUPIED);
    int  energy = still ? stEnergy : mvEnergy;
    const char* sc = (energy > 60) ? "hi" : (energy > 30) ? "md" : "lo";

    String html;
    html.reserve(1024);
    html = HTML_HEAD;
    html += "<body><div class='card " + String(occ ? "occ" : "idl") + "'>";
    html += "<div class='emoji'>" + String(occ ? "&#128701;" : "&#9989;") + "</div>";
    html += "<h1>解忧所</h1>";
    html += "<div class='status " + String(occ ? "red" : "grn") + "'>" + String(occ ? "使用中" : "空闲") + "</div>";
    if (occ) {
        unsigned long d = (millis() - occupiedSinceMs) / 1000;
        char b[16];
        snprintf(b, sizeof(b), "%02lu:%02lu:%02lu", d / 3600, (d % 3600) / 60, d % 60);
        html += "<div class='timer'>" + String(b) + "</div>";
    }
    html += "<div class='info'>信号 " + String(energy) + "%  ";
    html += "<span class='sig " + String(sc) + "' style='width:" + String(energy) + "px'></span></div>";
    html += "</div></body></html>";
    server.send(200, "text/html; charset=utf-8", html);
}

void handleApi() {
    unsigned long d = (currentState == OCCUPIED) ? (millis() - occupiedSinceMs) / 1000 : 0;
    String j; j.reserve(200);
    j  = "{";
    j += "\"occupied\":"          + String(currentState == OCCUPIED ? "true" : "false") + ",";
    j += "\"duration_seconds\":"  + String(d)  + ",";
    j += "\"has_moving\":"        + String(moving ? "true" : "false") + ",";
    j += "\"has_stationary\":"    + String(still  ? "true" : "false") + ",";
    j += "\"moving_distance\":"   + String(mvDist)  + ",";
    j += "\"moving_energy\":"     + String(mvEnergy) + ",";
    j += "\"stationary_distance\":" + String(stDist) + ",";
    j += "\"stationary_energy\":" + String(stEnergy) + ",";
    j += "\"detection_distance\":" + String(detDist);
    j += "}";
    server.send(200, "application/json", j);
}

// ===================== 初始化 =====================
void setup() {
    Serial.begin(115200);
    delay(300);

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_wqy12_t_chinese1);
    u8g2.drawUTF8(34, 34, "Starting...");
    u8g2.sendBuffer();

    Serial2.begin(256000, SERIAL_8N1, LD2410_RX, LD2410_TX);
    delay(3000);
    if (!radar.begin(Serial2)) {
        Serial.println("[LD2410] 初始化失败, 检查接线");
    } else {
        radar.setMaxValues(MAX_MOVING_GATE, MAX_STATIONARY_GATE, LD2410_TIMEOUT_SEC);
        Serial.printf("[LD2410] 移动%u门 静止%u门 超时%us\n",
                      MAX_MOVING_GATE, MAX_STATIONARY_GATE, LD2410_TIMEOUT_SEC);
    }

    connectWiFi();

    server.on("/", handleRoot);
    server.on("/api/status", handleApi);
    server.onNotFound([](){ server.send(404, "text/plain", "404"); });
    server.begin();
    Serial.println("[HTTP] 已启动");

    Serial.println("=== 解忧所指示器已就绪 ===\n");
    drawScreen();
}

// ===================== 主循环 =====================
void loop() {
    static int consecFails = 0;
    if (!radar.read()) {
        if (++consecFails == 200)
            Serial.printf("[LD2410] 连续 %d 帧失败\n", consecFails);
    } else {
        consecFails = 0;
    }
    moving   = radar.movingTargetDetected();
    still    = radar.stationaryTargetDetected();
    mvDist   = radar.movingTargetDistance();
    mvEnergy = radar.movingTargetEnergy();
    stDist   = radar.stationaryTargetDistance();
    stEnergy = radar.stationaryTargetEnergy();
    detDist  = radar.detectionDistance();

    updateState(radar.presenceDetected());

    if (millis() - lastDisplayMs >= DISPLAY_UPDATE_MS) {
        lastDisplayMs = millis();
        drawScreen();
    }

    server.handleClient();

    static unsigned long lastWifiCheck    = 0;
    static unsigned long reconnectStartMs = 0;
    static bool   wifiReconnecting = false;
    if (millis() - lastWifiCheck > 15000) {
        lastWifiCheck = millis();
        if (WiFi.status() == WL_CONNECTED) {
            if (wifiReconnecting) {
                wifiReconnecting = false;
                Serial.println("[WiFi] 重连成功");
            }
            if (!mdnsStarted && MDNS.begin(HOSTNAME)) {
                Serial.printf("[mDNS] http://%s.local\n", HOSTNAME);
                MDNS.addService("http", "tcp", 80);
                mdnsStarted = true;
            }
        } else {
            if (!wifiReconnecting) {
                wifiReconnecting = true;
                reconnectStartMs = millis();
                Serial.println("[WiFi] 断线, 后台重连...");
                WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
            } else if (millis() - reconnectStartMs > 60000) {
                wifiReconnecting = false;
                Serial.println("[WiFi] 重连超时, 重新发起...");
            }
        }
    }

    delay(50);
}
