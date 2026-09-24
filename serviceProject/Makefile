# ESP32-S3 N8R8 LED 游戏机主机 Makefile
# 智能上传：先尝试 USB，失败则自动切换到 OTA

.PHONY: build upload monitor ota clean smart-upload help

# 编译项目
build:
	pio run -e esp32-s3-n8r8

# USB 上传（CH343 串口）
upload:
	@echo "=== USB 上传模式 (esp32-s3-n8r8) ==="
	pio run -e esp32-s3-n8r8 -t upload

# 智能上传：先尝试 USB，失败则自动切换到 OTA
smart-upload:
	@echo "=== 智能上传模式 ==="
	@echo "步骤1: 尝试 USB 上传..."
	@pio run -e esp32-s3-n8r8 -t upload || ( \
		echo ""; \
		echo "USB 上传失败，自动切换到 OTA 模式..."; \
		echo "步骤2: 尝试 OTA 上传（环境: esp32-s3-n8r8-ota）..."; \
		if [ -n "$(IP)" ]; then \
			echo "使用 IP: $(IP)"; \
			UPLOAD_PORT=$(IP) pio run -e esp32-s3-n8r8-ota -t upload; \
		else \
			pio run -e esp32-s3-n8r8-ota -t upload || echo "OTA 失败可用: make ota IP=设备IP"; \
		fi \
	)

# 串口监控
monitor:
	pio device monitor -e esp32-s3-n8r8

# 编译并上传（USB）
all: build upload

# OTA 上传（默认 ESP32-S3-LED.local；mDNS 失败请用 make ota IP=192.168.x.x）
ota:
	@if [ -z "$(IP)" ]; then \
		echo "使用 platformio.ini 中的 upload_port (ESP32-S3-LED.local)"; \
		echo "若报 Host Not Found，请用: make ota IP=设备IP"; \
		pio run -e esp32-s3-n8r8-ota -t upload; \
	else \
		echo "使用 IP 上传: $(IP)"; \
		UPLOAD_PORT=$(IP) pio run -e esp32-s3-n8r8-ota -t upload; \
	fi

# 快速查看 IP 地址
show-ip:
	@echo "正在查找 ESP32 的 IP 地址..."
	@echo "如果看不到，请按一下开发板的 Reset 按钮"
	@timeout 10 pio device monitor -e esp32-s3-n8r8 2>/dev/null | grep -E "IP 地址|IP address|WiFi IP|本机 IP" | head -5 || echo "未找到IP，请检查连接或按Reset按钮"

# 清理编译文件
clean:
	pio run -e esp32-s3-n8r8 -t clean

# 完整清理
cleanall:
	pio run -t cleanall

# 帮助信息
help:
	@echo "可用命令:"
	@echo "  make build         - 编译项目 (esp32-s3-n8r8)"
	@echo "  make upload        - USB 上传程序"
	@echo "  make smart-upload  - 智能上传（先USB，失败则OTA）"
	@echo "  make ota           - OTA 上传（ESP32-S3-LED.local）"
	@echo "  make ota IP=xxx    - OTA 上传到指定 IP"
	@echo "  make monitor       - 打开串口监控"
	@echo "  make show-ip       - 查看 ESP32 的 IP 地址"
	@echo "  make all           - 编译并 USB 上传"
	@echo "  make clean         - 清理编译文件"
	@echo ""
	@echo "推荐首次烧录: make upload"
