# 臭臭 PlayTool 总 Makefile：一次 OTA 三个设备（开关 A、开关 B、主机）
# 用法：在项目根目录 chouchouPlayTool 下执行
#   make ota-all
# 若 mDNS 解析不到 .local，可指定 IP：
#   make ota-all IP_A=192.168.x.x IP_B=192.168.x.x IP_HOST=192.168.x.x
# 或只给其中某个：make ota-all IP_B=192.168.1.2

.PHONY: ota-all build-all help

ota-all:
	@echo "========== 1/3 开关 A (chouchouswitchA) =========="
	@$(MAKE) -C chouchouswitchA ota IP=$(IP_A) || exit 1
	@echo ""
	@echo "========== 2/3 开关 B (chouchouswitchB) =========="
	@$(MAKE) -C chouchouswitchB ota IP=$(IP_B) || exit 1
	@echo ""
	@echo "========== 3/3 主机 (serviceProject) =========="
	@$(MAKE) -C serviceProject ota IP=$(IP_HOST) || exit 1
	@echo ""
	@echo "========== 三个设备 OTA 全部完成 =========="

build-all:
	@$(MAKE) -C chouchouswitchA build
	@$(MAKE) -C chouchouswitchB build
	@$(MAKE) -C serviceProject build

help:
	@echo "用法（在 chouchouPlayTool 目录下）："
	@echo "  make ota-all              按顺序 OTA：开关A → 开关B → 主机（均用各项目 platformio.ini 的 .local）"
	@echo "  make ota-all IP_A=xxx      仅开关 A 用指定 IP，其余用 .local"
	@echo "  make ota-all IP_B=xxx      仅开关 B 用指定 IP"
	@echo "  make ota-all IP_HOST=xxx   仅主机用指定 IP"
	@echo "  make ota-all IP_A=... IP_B=... IP_HOST=...  三个都指定 IP（mDNS 失败时用）"
	@echo "  make build-all             仅编译三个项目，不上传"
