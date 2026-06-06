# ESP32-C3 LED游戏机项目 Makefile
# 智能上传：先尝试USB，失败则自动切换到OTA

.PHONY: build upload monitor ota clean smart-upload help

# 编译项目
build:
	pio run

# USB上传（只使用USB）
upload:
	@echo "=== USB上传模式 ==="
	pio run -t upload

# 智能上传：先尝试USB，失败则自动切换到OTA
smart-upload:
	@echo "=== 智能上传模式 ==="
	@echo "步骤1: 尝试USB上传..."
	@pio run -t upload || ( \
		echo ""; \
		echo "USB上传失败，自动切换到OTA模式..."; \
		echo "步骤2: 尝试OTA上传（使用环境: esp32-c3-devkitm-1-ota）..."; \
		if [ -n "$(IP)" ]; then \
			echo "使用 IP: $(IP)"; \
			UPLOAD_PORT=$(IP) pio run -e esp32-c3-devkitm-1-ota -t upload; \
		else \
			pio run -e esp32-c3-devkitm-1-ota -t upload || echo "OTA 失败可用: make ota IP=设备IP"; \
		fi \
	)

# 串口监控
monitor:
	pio device monitor

# 编译并上传（USB）
all: build upload

# OTA 上传（默认用 platformio.ini 的 ESP32-C3-LED.local；若 mDNS 失败请用 make ota IP=192.168.x.x）
ota:
	@if [ -z "$(IP)" ]; then \
		echo "使用 platformio.ini 中的 upload_port (ESP32-C3-LED.local)"; \
		echo "若报 Host Not Found，请用: make ota IP=设备IP"; \
		pio run -e esp32-c3-devkitm-1-ota -t upload; \
	else \
		echo "使用 IP 上传: $(IP)"; \
		UPLOAD_PORT=$(IP) pio run -e esp32-c3-devkitm-1-ota -t upload; \
	fi

# 快速查看IP地址
show-ip:
	@echo "正在查找ESP32的IP地址..."
	@echo "如果看不到，请按一下开发板的Reset按钮"
	@timeout 10 pio device monitor 2>/dev/null | grep -E "IP 地址|IP address|WiFi IP" | head -5 || echo "未找到IP，请检查连接或按Reset按钮"

# 清理编译文件
clean:
	pio run -t clean

# 完整清理
cleanall:
	pio run -t cleanall

# 帮助信息
help:
	@echo "可用命令:"
	@echo "  make build         - 编译项目"
	@echo "  make upload        - USB上传程序"
	@echo "  make smart-upload   - 智能上传（先USB，失败则OTA）"
	@echo "  make ota           - OTA上传（使用默认IP: 192.168.1.11）"
	@echo "  make ota IP=xxx    - OTA上传到指定IP（例如: make ota IP=192.168.1.100）"
	@echo "  make monitor       - 打开串口监控"
	@echo "  make show-ip       - 查看ESP32的IP地址"
	@echo "  make all           - 编译并USB上传"
	@echo "  make clean         - 清理编译文件"
	@echo ""
	@echo "推荐使用: make smart-upload"
