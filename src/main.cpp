/**
 * 解忧所 — 卫生间占用指示器 (精简版)
 *
 * 硬件: ESP32 + LD2410C + SH1106 OLED
 * 供电: USB-C 5V
 */

// ===================== 依赖 =====================
#include <Arduino.h>
#include <ld2410.h>
#include <U8g2lib.h>
#include <math.h>

#ifndef PI
#define PI 3.14159265358979323846
#endif

// ===================== 用户配置 =====================

// 引脚
#define LD2410_RX  16
#define LD2410_TX  17
#define OLED_SDA    21
#define OLED_SCL    22

// 时间常数
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

// ===================== 状态变量 =====================
enum State { IDLE, OCCUPIED };
State        currentState     = IDLE;
unsigned long presenceStartMs = 0;
unsigned long absenceStartMs  = 0;
unsigned long occupiedSinceMs = 0;
unsigned long lastDisplayMs   = 0;

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
            }
        }
    } else {
        presenceStartMs = 0;
        if (currentState == OCCUPIED) {
            if (!absenceStartMs) absenceStartMs = now;
            if (now - absenceStartMs >= ABSENCE_DEBOUNCE_MS) {
                currentState = IDLE;
            }
        }
    }
}

// ===================== 初始化 =====================
void setup() {
    Serial.begin(115200);

    u8g2.begin();
    u8g2.clearBuffer();
    u8g2.setFont(u8g2_font_6x12_tr);
    u8g2.drawStr(28, 34, "Starting...");
    u8g2.sendBuffer();

    Serial2.begin(256000, SERIAL_8N1, LD2410_RX, LD2410_TX);
    delay(3000);
    if (!radar.begin(Serial2)) {
        Serial.println("[LD2410] init fail, check wiring");
    } else {
        radar.setMaxValues(MAX_MOVING_GATE, MAX_STATIONARY_GATE, LD2410_TIMEOUT_SEC);
        Serial.printf("[LD2410] move gate %u, still gate %u, timeout %us\n",
                      MAX_MOVING_GATE, MAX_STATIONARY_GATE, LD2410_TIMEOUT_SEC);
    }

    Serial.println("=== sensorSwitch ready ===");
    drawScreen();
}

// ===================== 主循环 =====================
void loop() {
    static int consecFails = 0;
    if (!radar.read()) {
        if (++consecFails == 200)
            Serial.printf("[LD2410] %d consecutive fails\n", consecFails);
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

    delay(50);
}
