# 板卡事实表（对话 01）

状态：**对话 01 已结项；上游 P4 基线已构建，openvela 整合与实板验证待完成。** 板卡实物、下载入口和现有厂商固件串口已核验；当前 openvela 对 ESP32-P4/P4X 的底层支持不存在，未验证项不是“未知也可用”，而是禁止作为实现前提。

| 项目 | 已验证值 | 证据位置 | 结论 |
|---|---|---|---|
| 开发板准确型号/修订版 | `ESP32-P4 Function_EV_Board V1.6` | `evidence/01-board-facts/board-front.jpg` 丝印 | 已确认 |
| SoC 准确型号/封装 | ESP32-P4，芯片修订 v3.2，40 MHz 晶振；封装待核验 | `serial-com8-rst-20260728.log`；2026-07-28 `esptool chip-id` 只读查询 | 部分确认 |
| Flash/PSRAM 容量 | 16 MB SPI Flash；32 MB PSRAM | 同上，启动日志的 `SPI Flash Size` 与 `Found 32MB PSRAM device` | 已确认 |
| 下载接口与工具 | USB Serial/JTAG；Windows 枚举为 COM8，VID `303A`/PID `1001`；ROM 下载器和 RAM stub 传输已验证；Flash 写入未测试 | `serial-com8-rst-20260728.log`；2026-07-28 `esptool chip-id` 只读查询 | 部分确认 |
| UART 端口、引脚、波特率 | COM8 以 115200 8N1 读到启动日志；固件报告 console UART 为 GPIO37/38 | `serial-com8-rst-20260728.log` | 已确认（现有厂商固件） |
| 板载 LED 和 GPIO 电平 | 仅确认 5 V 电源指示灯；无可编程板载 LED 证据。外接 LED 候选为 J1-3/GPIO7，电平逻辑未验证 | 官方 P4X 用户指南第 13 页（J1-3 为 GPIO7）；后续实测 | 部分确认 |
| 相机连接器/传感器 | MIPI Camera 连接器存在；传感器与可用性未验证 | `evidence/01-board-facts/board-front.jpg` | 待核验 |
| 显示连接器/面板 | MIPI Display 连接器存在；现有固件完成 MIPI DSI 初始化，但 GT911 触摸初始化失败 | `board-front.jpg`；`serial-com8-rst-20260728.log` | 未验证 |
| openvela 源码与 P4 支持现状 | VM 工作区 `/home/max/openvela`；清单为 `https://gitee.com/open-vela/manifests.git` 默认分支、提交 `839ffb361b07ccd5a02b385e58e4ffe5bf197d14`；已同步 NuttX 提交 `76354c637858ecb0aa4601629327acb6f44a26bb`。该树无 P4 路径。Apache NuttX `5a7f1b5005acdd810cadc20340684dfa6b8670d1` 有同款板目录，且其 16 MB `nsh` 镜像已在独立工作区构建成功 | `evidence/01-board-facts/openvela-source-inspection-20260729.log`；`docs/research/esp32-p4-upstream-build.md` | 当前 openvela 无 P4 BSP；上游 P4 基线可用但尚未整合 |

## 对话 01 交接

- 适配调查与阻塞项见 `docs/bsp-adaptation-checklist.md`。
- 阶段结论、冲突和对话 02 的准确输入见 `docs/conversation-01-conclusion.md`。

## 2026-07-29 real-board baseline update

- The upstream Apache NuttX `esp32p4-function-ev-board:nsh` baseline reached
  the `NuttShell (NSH)` prompt on the physical ESP32-P4 Function_EV_Board V1.6.
- Authoritative capture: `evidence/01-board-facts/serial-nuttx-nsh-20260729.log`.
- The verified boot used a digest-aware simple-boot image and a temporary
  upstream-reference patch that skips the appended image SHA-256 before
  scanning hidden Flash segments. This patch must be reviewed and reproduced
  as part of any later integration; it is not an openvela BSP change.
- This result does not validate openvela boot, GPIO/LED behavior, camera,
  display, or stable VMware USB passthrough.

## 2026-07-29 interactive NSH update

- The physical board accepted UART input and returned NSH command output after
  the CH340 TX/RX loopback was verified.
- `free` reported 479744 bytes free in `Umem`; `ls /dev` listed `console`,
  `null`, `random`, `ttyS0`, and `zero`.
- Evidence: `evidence/01-board-facts/serial-nuttx-nsh-interaction-20260729.log`.
- `uname` is not compiled into this minimal NSH image; its "command not found"
  response confirms command input reached NSH, not a boot failure.
