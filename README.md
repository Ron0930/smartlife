## smartlife: 基于 ESP32 的物联网智能门锁项目

### 项目简介

`smartlife` 是一个基于 ESP32 开发板的智能门锁物联网项目，通过集成多种开锁方式与实时状态反馈，实现便捷的门禁管理。设备可以单路继电器的方式上物联网云平台ThingsCloud。

### 功能特性

* **IC 卡识别开锁**
  支持常见的 MIFARE Classic、Ultralight 等 IC 卡读写，通过 PN532 模块实现卡片识别开锁。
* **手机 APP 二维码识别开锁**
  手机端生成或扫码二维码，实现远程或近场扫码开锁功能。
* **手机 APP 一键开锁**
  在手机 APP 内通过简单的按钮操作，即可发送指令一键开锁。
* **自动上报开锁记录**
  实时将二维码开锁与 IC 卡开锁记录上报至后端或云端，实现日志管理与审计。
* **开锁状态与结果显示**
  OLED/LCD 屏幕刷新显示当前开锁动作及成功/失败状态，便于现场监控。
* **Wi-Fi 连接状态显示**
  实时显示设备的 Wi-Fi 连接状态，支持断线重连与网络指示。

### 硬件需求

* **ESP32 开发板**
* **PN532 NFC 模块**（I2C 接口）
* **OLED 显示屏**
* **继电器模块**（驱动门锁）
* **XM1605 二维码扫描模块**
* **斜口电磁锁**
* 电源与连线若干

### 硬件接线

1. **PN532 NFC 模块（I²C，Wire 0）**

   * VCC → ESP32 3V3
   * GND → ESP32 GND
   * SCL → ESP32 GPIO 22
   * SDA → ESP32 GPIO 21

2. **SSD1306 OLED 显示屏（I²C，Wire 1）**

   * VCC → ESP32 3V3
   * GND → ESP32 GND
   * SCL → ESP32 GPIO 15
   * SDA → ESP32 GPIO 4

3. **QR 码串口模块（UART2）**

   * VCC → ESP32 3V3
   * GND → ESP32 GND
   * TXD → ESP32 GPIO 16 (Serial2 RX)
   * RXD → ESP32 GPIO 17 (Serial2 TX)

4. **继电器模块**

   * VCC → ESP32 3V3（或模块要求电压）
   * GND → ESP32 GND
   * IN → ESP32 GPIO 5（低电平触发）

### 软件需求

* **Arduino IDE** 或 **PlatformIO**
* ESP32 开发套件库（`esp32`）
* PN532 驱动库（`Adafruit_PN532`）
* 显示屏驱动库（例如 `Adafruit_SSD1306` / `U8g2`）
* 网络与 JSON 库（`WiFi.h`、`ArduinoJson` 等）

### 项目结构

```
smartlife/
│  ├─ src/            # 源代码
│  ├─ platformio.ini  # PlatformIO 配置
│  └─ README.md       # 固件说明
```

### 安装与使用

1. 配置 Wi-Fi 名称和密码以及后端地址。
2. 编译并上传固件到 ESP32。
3. 上电后，设备启动并尝试连接 Wi-Fi。
4. 将 IC 卡贴近读卡区测试开锁。
5. 在手机 APP 中扫码二维码或点击“一键开锁”按钮测试。
6. 观察屏幕上的连接与开锁状态指示。

### 贡献指南

欢迎提交 Issue 和 Pull Request：

* Fork 本仓库并创建新分支
* 提交改进或新功能
* 发起 PR 并描述更改
