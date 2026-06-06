# WS2812B LED灯带游戏机 - ESP32-C3项目

这是一个基于ESP32-C3和WS2812B灯带的互动游戏项目，用于陪孩子玩耍，锻炼往返跑能力。

## 硬件配置

- **LED灯带**: WS2812B 5050全彩LED，96灯/米
- **主控**: ESP32-C3 superMini
- **电源**: 5V 40A（用于LED灯带供电）
- **LED数量**: 96个（可根据实际长度调整）

## 接线说明

### ESP32-C3 连接
- **LED数据线**: 连接到 GPIO2（可在代码中修改 `LED_PIN`）
- **电源**: 
  - ESP32-C3: 通过USB或5V供电
  - LED灯带: 使用5V 40A电源独立供电
  - **重要**: LED灯带的GND必须与ESP32-C3的GND共地

### 接线图
```
5V 40A电源 ──┬── LED灯带 VCC
             │
             └── LED灯带 GND ── ESP32-C3 GND (共地)

ESP32-C3 GPIO2 ── LED灯带 DIN (数据输入)
```

## 项目结构

```
serviceProject/
├── platformio.ini      # PlatformIO配置文件
├── src/
│   └── main.cpp        # 主程序代码
├── .gitignore          # Git忽略文件
└── README.md           # 本文件
```

## 环境搭建

### 1. 安装PlatformIO

如果使用VS Code：
1. 安装PlatformIO IDE扩展
2. 打开项目文件夹

如果使用命令行：
```bash
# 安装PlatformIO Core
pip install platformio

# 进入项目目录
cd serviceProject
```

### 2. 配置WiFi（用于OTA）

编辑 `src/main.cpp`，修改以下配置：
```cpp
const char* ssid = "YOUR_WIFI_SSID";      // 你的WiFi名称
const char* password = "YOUR_WIFI_PASSWORD"; // 你的WiFi密码
```

### 3. 配置LED参数

在 `src/main.cpp` 中可根据实际情况修改：
```cpp
#define LED_PIN 2          // LED数据引脚（默认GPIO2）
#define NUM_LEDS 96        // LED数量（根据实际长度调整）
#define BRIGHTNESS 50      // 初始亮度（0-255）
```

## 编译和上传

### 首次上传（通过USB）

1. 连接ESP32-C3到电脑
2. 编译并上传：
```bash
pio run -t upload
```

3. 查看串口输出：
```bash
pio device monitor
```

### OTA升级（无线更新）

首次通过USB上传后，之后可以通过OTA无线更新：

1. 确保ESP32-C3已连接到WiFi
2. 查看串口输出获取IP地址
3. 使用PlatformIO OTA上传：
```bash
pio run -t upload --upload-port <ESP32的IP地址>
```

或者使用Arduino IDE的OTA功能：
- 工具 → 端口 → 选择 "ESP32-C3-LED at <IP地址>"
- 点击上传

**OTA密码**: 12345678（可在代码中修改）

## 测试功能

程序启动后默认进入"全部点亮"模式。可以通过串口命令切换模式：

### 串口命令

打开串口监视器（115200波特率），输入以下命令：

- `1` - 切换到**全部点亮模式**（所有LED显示白色）
- `2` - 切换到**逐个点亮模式**（LED逐个点亮，显示彩虹色）
- `brightness <0-255>` - 设置亮度，例如：`brightness 100`
- `status` - 查看当前状态（LED数量、亮度、模式、WiFi信息）

### 测试步骤

1. **全部点亮测试**
   - 上传程序后，所有LED应该显示白色
   - 如果部分LED不亮，检查接线和电源
   - 如果颜色不对，可能需要调整 `COLOR_ORDER`（GRB/RGB）

2. **逐个点亮测试**
   - 在串口输入 `2`，LED应该逐个点亮，显示彩虹色
   - 观察是否有LED损坏或颜色异常

3. **亮度测试**
   - 尝试不同的亮度值：`brightness 50`、`brightness 100`、`brightness 255`
   - 根据实际效果选择合适的亮度

## 上传问题排查

### 串口连接问题

如果遇到 "Failed to connect to ESP32-C3" 或 "No serial data received" 错误：

1. **检查ESP32是否连接**
   ```bash
   pio device list
   ```
   应该能看到类似 `/dev/cu.usbserial-xxxxx` 或 `/dev/cu.wchusbserialxxxxx` 的端口

2. **如果看不到ESP32端口**：
   - 检查USB线是否支持数据传输（有些线只能充电）
   - 尝试更换USB端口
   - 检查USB驱动是否安装（Mac通常不需要，Windows可能需要CH340/CP2102驱动）

3. **手动指定串口**：
   编辑 `platformio.ini`，取消注释并填入正确的端口：
   ```ini
   upload_port = /dev/cu.usbserial-xxxxx
   monitor_port = /dev/cu.usbserial-xxxxx
   ```

4. **ESP32-C3进入下载模式**：
   - 某些情况下需要手动进入下载模式
   - 按住 **BOOT** 按钮（如果有的话）
   - 按一下 **RESET** 按钮
   - 松开 **BOOT** 按钮
   - 然后立即执行上传命令

5. **如果自动检测到蓝牙端口**：
   - 这是正常的，但需要手动指定正确的串口
   - 使用 `pio device list` 查看所有端口
   - 选择名称包含 `usb`、`serial`、`ch340`、`cp210` 等的端口

## 常见问题

### 1. LED不亮或部分不亮
- 检查电源是否足够（5V 40A应该足够96个LED）
- 检查GND是否共地
- 检查数据线是否连接到正确的GPIO
- 尝试降低亮度：`brightness 30`

### 2. 颜色不对
- WS2812B通常是GRB顺序，如果颜色不对，尝试修改 `COLOR_ORDER` 为 `RGB`

### 3. WiFi连接失败
- 检查WiFi名称和密码是否正确
- 确保WiFi是2.4GHz（ESP32不支持5GHz）
- OTA功能需要WiFi，但不影响LED基本功能

### 4. OTA上传失败
- 确保ESP32-C3和电脑在同一WiFi网络
- 检查防火墙设置
- 首次必须通过USB上传，之后才能使用OTA

## 下一步开发

当前版本是测试版本，后续需要实现：

1. **游戏逻辑**
   - A端定时产生随机颜色的光点
   - B端按钮检测（两个按钮，间隔一定距离）
   - 光点相遇检测和消失逻辑
   - 得分和难度系统

2. **硬件扩展**
   - 添加两个按钮输入（GPIO）
   - 可能需要添加上拉/下拉电阻

## 技术支持

如有问题，请检查：
1. 串口输出信息
2. LED接线和电源
3. WiFi配置
4. 代码中的配置参数

## 许可证

本项目仅供学习和个人使用。
