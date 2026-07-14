# 解忧所 — 卫生间占用指示器

LD2410C 毫米波人体存在传感器 + ESP32 + SH1106 OLED 的卫生间占用状态指示器。
传感器检测人体存在（含静止/呼吸时微动），OLED 屏挂在门外显示 `BUSY` / `FREE`，

---

## 硬件清单

| 编号 | 元件   | 型号                              | 数量 |
| ---- | ------ | --------------------------------- | ---- |
| 1    | 主控   | ESP32 开发板 (CH9102/USB-C/30Pin) | 1    |
| 2    | 传感器 | HLK-LD2410C 毫米波人体存在模块    | 1    |
| 3    | 屏幕   | SH1106 OLED 1.3 寸 128×64 I2C     | 1    |
| 4    | 面包板 | MB-102 830 孔（含公对公跳线）     | 1    |
| 5    | 杜邦线 | 母对公 40P 一排                   | 1    |
| 6    | 电源   | 5V 1A USB 手机充电头              | 1    |
| 7    | 数据线 | USB-A to USB-C                    | 1    |
| 8    | 外壳   | 200×120×75mm ABS 接线盒           | 1    |

---

## 面包板布局说明

ESP32 不插板上，USB-C 朝右，上排针需要落在上半区，底排针需要落在下半区，所有信号线/电源线的连接全部走母对公杜邦线在面包板连接，不用公对公。

- **上红轨** → 5V，给 LD2410C 供电
- **上蓝轨** → GND
- **下红轨** → 3.3V，给 OLED 供电
- **下蓝轨** → GND

---

## 接线表

线① ESP32 VIN(底排最右针) → 面包板 上红轨 任意孔
线② ESP32 GND(底排右数第2针) → 面包板 上蓝轨 任意孔
线③ ESP32 3V3(上排最右针) → 面包板 下红轨 任意孔
线④ ESP32 GND(上排右数第2针) → 面包板 下蓝轨 任意孔
线⑤ 上红轨 任意孔→ LD2410C VCC
线⑥ 上蓝轨 任意孔 → LD2410C GND
线⑦ D16/RX2 → LD2410C TX
线⑧ D17/TX2 → LD2410C RX
线⑨ D21 → OLED SDA
线⑩ D22 → OLED SCL
线⑪ 下红轨 任意孔 → OLED VCC
线⑫ 下蓝轨 任意孔 → OLED GND

---

## 编译 & 烧录

1. 安装 [VS Code](https://code.visualstudio.com/) + **PlatformIO IDE** 扩展
2. 打开 `sensorSwitch/` 文件夹
3. 修改 `src/main.cpp` 顶部 WiFi：

```cpp
const char* WIFI_SSID     = "你的WiFi名";
const char* WIFI_PASSWORD = "你的WiFi密码";
```

4. 底栏点击 **→ Upload** 烧录
5. 串口监视器 (115200 baud) 查看运行日志

---

## 使用

| 场景 | OLED 显示               | 网页 (bathroom.local)    |
| ---- | ----------------------- | ------------------------ |
| 有人 | `BUSY` + 底部心跳节奏条 | 红色卡片 «使用中» + 计时 |
| 无人 | `FREE` + 笑脸图标       | 绿色卡片 «空闲»          |

### API

`GET http://bathroom.local/api/status` 返回 JSON：

```json
{
  "occupied": true,
  "duration_seconds": 272,
  "has_moving": true,
  "has_stationary": false,
  "moving_distance": 120,
  "moving_energy": 85,
  "stationary_distance": 0,
  "stationary_energy": 0,
  "detection_distance": 450
}
```

---

## LD2410C 参数

| 参数                  | 当前值 | 含义           |
| --------------------- | ------ | -------------- |
| `MAX_MOVING_GATE`     | 4      | 移动检测 3m    |
| `MAX_STATIONARY_GATE` | 3      | 静止检测 2.25m |
| `LD2410_TIMEOUT_SEC`  | 3      | 无人后 3s 报离 |

传感器同时支持 **HLKRadarTool**（手机蓝牙）直接配参，覆盖距离门灵敏度和超时时间，掉电不丢失。

---

## 依赖

- [ncmreynolds/ld2410](https://github.com/ncmreynolds/ld2410) — LD2410 串口驱动
- [olikraus/U8g2](https://github.com/olikraus/u8g2) — OLED 图形库
