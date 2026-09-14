# ESP32-P4 Energy Label AI Inspection Terminal - camera_diag

## Overview

This application provides an AI-powered energy label inspection terminal
for the ESP32-P4 Function EV Board running OpenVela (NuttX). It implements
an auto-starting persistent UI with camera capture, touch interaction,
TFLite Micro AI inference, and a four-stage workflow.

## Hardware Requirements

| Component | Model | Interface |
|-----------|-------|-----------|
| SoC | ESP32-P4 (v3.2, dual-core RISC-V @ 400MHz) | - |
| PSRAM | 32MB external RAM | Memory mapped at 0x48000000 |
| Flash | 4MB SPI (DIO, 80MHz) | esptool.py @ 0x2000 |
| Camera | SC2336 (1280x720 RAW8 @30fps) | MIPI-CSI 2-lane |
| Display | EK79007 (1024x600 RGB565) | MIPI-DSI |
| Touch | GT911 (5-point capacitive) | I2C0 (GPIO8/7) |

## Build

```bash
# In the OpenVela workspace root:
./build.sh esp32p4-function-ev-board:nsh menuconfig
# Enable: Examples → ESP32-P4 Energy Label AI Inspection Terminal
# Build:
./build.sh esp32p4-function-ev-board:nsh -j4
```

## Flash

```bash
cd nuttx
esptool.py --chip esp32p4 --port /dev/ttyACM0 --baud 460800 \
  write-flash 0x2000 nuttx.bin
```

## Usage

After boot, the NSH shell starts and the auto-start script launches the UI:

```
nsh> camera_diag --dsi-ui-live
nsh> camera_diag --touch-ui-live
```

Workflow stages (touch UI buttons):
1. **CAPTURE** - Capture camera frame
2. **GALLERY** - Preview captured images
3. **LABEL** - Detect label region (YOLO inference)
4. **ENERGY** - Classify energy efficiency level

## Source Files

| File | Description |
|------|-------------|
| `camera_diag_main.c` | Main entry, command parser, workflow state machine |
| `esp32p4_dsi.c/h` | MIPI-DSI display driver, GDMA frame submission, UI drawing |
| `esp32p4_csi.c/h` | MIPI-CSI camera driver |
| `esp32p4_csi_isp.c/h` | ISP processor integration (RAW8→RGB565) |
| `sc2336.c` | SC2336 sensor driver (SCCB/I2C) |
| `camera_diag_tflm_init.cc/h` | TFLite Micro runtime initialization and inference |
| `src/` | Core inspection library (decision, event_log, inspection, parameter) |
| `include/` | Core library headers |
| `rcS` | ROMFS auto-start script |

## Directory Structure

```
app/camera_diag/      - Main application source
board/contest_board/  - ESP32-P4 board configuration
tools/                - Build, flash, test utility scripts
docs/                 - Project documentation and progress ledger
models/               - YOLO model weights and configs
evidence/             - Test logs and board verification evidence
artifacts/            - Firmware binaries
logs/                 - AI Coding session logs
```