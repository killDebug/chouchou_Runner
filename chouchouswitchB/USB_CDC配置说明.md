# ESP32-C3 USB CDC 配置说明

本开关子板（switchA）使用与服务端（serviceProject）相同的 ESP32-C3 Super Mini 配置。

## 关键配置（platformio.ini）

```ini
-DARDUINO_USB_MODE=1
-DARDUINO_USB_CDC_ON_BOOT=1
```

- **USB CDC 模式**：ESP32-C3 内置 USB，无需外接 CH340/CP2102。
- **启动时启用**：保证 `Serial` 在 `setup()` 前可用，串口监视器能正常看到输出。

## 上传与监视

- **USB 上传**：选环境 `esp32-c3-devkitm-1`，插上开关板后若端口不同，请修改 `upload_port` / `monitor_port`（如 `/dev/cu.usbmodem2402`）。
- **OTA 上传**：选环境 `esp32-c3-devkitm-1-ota`，将 `upload_port` 改为本机实际 IP（设备连上 WiFi 后会在串口打印 IP）。

详细说明见服务端项目中的 `USB_CDC配置说明.md`。
