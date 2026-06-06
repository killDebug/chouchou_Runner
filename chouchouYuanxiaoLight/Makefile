# 开关 A (chouchouswitchA) - ESP32-C3 Super Mini
# make upload / make ota / make monitor 等

.PHONY: build upload monitor ota clean smart-upload help show-ip all cleanall

# 编译
build:
	pio run

# USB 上传
upload:
	@echo "=== USB 上传 ==="
	pio run -t upload

# 智能上传：先 USB，失败再 OTA
smart-upload:
	@echo "=== 智能上传 ==="
	@echo "步骤1: 尝试 USB..."
	@pio run -t upload || ( \
		echo ""; \
		echo "USB 失败，尝试 OTA..."; \
		if [ -z "$(IP)" ]; then \
			echo "使用 platformio.ini 中的 IP"; \
			pio run -e esp32-c3-devkitm-1-ota -t upload || echo "OTA 失败，请改 platformio.ini 里 upload_port 为设备 IP"; \
		else \
			echo "使用 IP: $(IP)"; \
			sed -i.bak "s|upload_port = .*|upload_port = $(IP)|" platformio.ini; \
			pio run -e esp32-c3-devkitm-1-ota -t upload; \
			git checkout platformio.ini 2>/dev/null || true; \
		fi \
	)

# 串口监控
monitor:
	pio device monitor

# 编译并 USB 上传
all: build upload

# OTA 上传（默认用 platformio.ini 的 IP，或 make ota IP=192.168.50.xxx）
ota:
	@if [ -z "$(IP)" ]; then \
		echo "使用 platformio.ini 中的 upload_port"; \
		pio run -e esp32-c3-devkitm-1-ota -t upload; \
	else \
		echo "使用 IP: $(IP)"; \
		sed -i.bak "s|upload_port = .*|upload_port = $(IP)|" platformio.ini; \
		pio run -e esp32-c3-devkitm-1-ota -t upload; \
		git checkout platformio.ini 2>/dev/null || true; \
	fi

# 查看设备 IP（上电后看串口输出）
show-ip:
	@echo "查看串口输出中的「本机 IP」..."
	@timeout 12 pio device monitor 2>/dev/null | grep -E "本机 IP|IP:" | head -3 || echo "未找到，请按 Reset 或检查 WiFi"

# 清理
clean:
	pio run -t clean

cleanall:
	pio run -t cleanall

help:
	@echo "开关 A (SwitchA) - 可用命令:"
	@echo "  make build         - 编译"
	@echo "  make upload        - USB 上传"
	@echo "  make smart-upload   - 先 USB 再 OTA"
	@echo "  make ota           - OTA 上传（用 platformio.ini 的 IP）"
	@echo "  make ota IP=xxx     - OTA 到指定 IP"
	@echo "  make monitor       - 串口监控"
	@echo "  make show-ip       - 尝试从串口读 IP"
	@echo "  make all           - 编译并 USB 上传"
	@echo "  make clean         - 清理"
	@echo ""
	@echo "灯环测试: 上传后打开 monitor，发 1/2/3/4 或 brightness 0-255"
