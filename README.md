# 基于 openvela 的 ESP32-P4 能效标签 AI 质检终端

**队伍：shushudui（鼠鼠队） | 赛道：新硬件平台适配 | 仓库：contest2026_235_shushudui**

---

## 一、作品简介

本项目在 **ESP32-P4 Function EV Board** 上基于 **openvela（NuttX）** 实现了一套完整的端侧 AI 能效标签质检终端。系统具备 **开机自动常驻 UI**、**MIPI-CSI 相机采集**、**TFLite Micro AI 推理**、**MIPI-DSI 触摸交互** 等能力，通过 CAPTURE → GALLERY → LABEL → ENERGY 四阶段工作流，实现能效标签的自动检测与分级。

关键词：openvela、ESP32-P4、TFLite Micro、YOLO、MIPI-CSI/DSI、GT911 触摸、端侧 AI 质检

---

## 二、选题方向

**新硬件平台适配**。本作品选择官方待适配开发板 ESP32-P4X-Function-EV-Board，基于 openvela 操作系统完成了从底层 BSP 移植、外设驱动适配（MIPI-CSI/ISP 相机、MIPI-DSI 显示、GT911 I2C 触摸、PSRAM 内存映射）到系统构建的全链路适配，使 openvela 在该 RISC-V 平台上正常启动并运行核心功能，并在适配基础上开发了端侧 AI 质检应用 Demo，形成从板级启动到应用闭环的完整验证。

---

## 三、目录结构

```
app/camera_diag/          — 主应用源码（camera_diag_main.c, esp32p4_dsi.c, TFLM初始化等）
  ├── src/                — 核心检测库（decision, event_log, inspection, parameter）
  └── include/            — 核心库头文件
board/contest_board/      — ESP32-P4 板级配置
tools/                    — 构建/烧录/测试工具脚本（70+ Python & Shell）
docs/                     — 项目文档、进度台账、交接文档
models/                   — YOLO 模型权重与配置
evidence/                 — 真机测试日志与板端验证证据
artifacts/                — 固件二进制产物（50+ 版本）
logs/                     — AI Coding 对话日志
contest2026_235_shushudui.xml — repo manifest 清单
```

---

## 四、运行方式

### 硬件要求

- ESP32-P4 Function EV Board (v1.6)
- SC2336 MIPI 相机模组
- EK79007 DSI 显示屏 (1024×600)
- GT911 电容触摸屏 (I2C)
- USB-Serial/JTAG 下载线 (COM口)

### 编译

```bash
# 在 openvela 工作区根目录
repo init -u https://github.com/Max-YxY/contest2026_235_shushudui \
  -b dev-ai-contest-2026 -m contest2026_235_shushudui.xml
repo sync -c -j8

# 编译 camera_diag 应用
cd nuttx
make -j4
```

### 烧录

```bash
esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 \
  write-flash 0x2000 nuttx.bin
```

### 使用

系统上电后自动启动 UI：

1. 触摸 **CAPTURE** 按钮 → 相机拍照
2. 触摸 **GALLERY** 按钮 → 预览已拍照片
3. 触摸 **LABEL** 按钮 → YOLO 检测标签区域
4. 触摸 **ENERGY** 按钮 → 能效等级分类

---

## 五、AI Coding 使用说明

本作品使用 **OpenAI Codex（ChatGPT 桌面版 Codex 模式）** 作为主要 AI 编程助手，覆盖全流程开发：

| 环节 | AI 辅助内容 |
|------|------------|
| 需求分析 | 项目范围界定、模块拆分、阶段规划 |
| 方案设计 | BSP 适配方案、CSI/DSI/触摸驱动架构设计 |
| 代码生成 | camera_diag_main.c, esp32p4_dsi.c, TFLM init 等 80%+ 代码 |
| 调试排错 | DSI 动态刷新、TFLM 分配器、触摸 Y 轴校准等问题定位 |
| 构建脚本 | Makefile/Kconfig、VM 远程编译流程、烧录脚本 |
| 文档撰写 | 项目进度台账、交接文档、提交材料 |

**MCP/Skills 使用**：SSH（远程编译机 openvela-vm-jul）、串口调试、SQLite 查询、文件操作。

完整 AI Coding 对话日志见 `logs/` 目录。

---

## 六、系统能力清单

| 能力类别 | 具体能力 | 状态 |
|---------|---------|------|
| 内核与启动 | NuttX NSH, ROMFS rcS 自启 | ✅ 完成 |
| 内存管理 | 32MB PSRAM, 外部 RAM 映射 | ✅ 完成 |
| I2C 驱动 | esp32p4_i2c (轮询模式) | ✅ 完成 |
| MIPI-CSI | SC2336 相机 @ 1280×720 | ✅ 完成 |
| ISP | RAW8→RGB565 demosaic | ✅ 完成 |
| MIPI-DSI | EK79007 1024×600 GDMA | ✅ 完成 |
| 触摸 | GT911 I2C 坐标校准 | ✅ 完成 |
| AI 推理 | TFLite Micro, YOLO 模型 | ✅ 完成 |
| 模型存储 | SPI Flash 容器 /* 0xE0000 | ✅ 完成 |
| 能效分类 | 96×96 ROI, 98.95% acc | ✅ 完成 |
| 缺陷检测 | Damage/Stain/Wrinkle | ⚠️ 部分完成 |
| 端到端性能 | 单帧推理 ~112s | ⚠️ 优化中 |

---

## 七、提交清单

- ✅ 作品介绍文档（.docx）
- ✅ AI Coding 日志导出至 `logs/`
- ✅ 源码提交至 `dev-ai-contest-2026` 分支
- ⬜ Demo 演示视频（需录制）
- ✅ 真机证据（串口日志、启动日志、相机原始帧）
- ✅ 使用赛事官方仓库 `contest2026_235_shushudui`