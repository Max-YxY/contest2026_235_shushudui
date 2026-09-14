---
name: openvela-esp32p4-build
description: >-
  ESP32-P4 OpenVela 固件远程编译、烧录与串口调试工作流。
  SSH 连接远程编译机（openvela-vm-jul）→ 同步源码 → 构建 → 烧录 → 串口验证。
  适用于基于 OpenVela/NuttX 的 ESP32-P4 嵌入式开发。
argument-hint: "[build|flash|test|sync|serial] ..."
---

# openvela-esp32p4-build — ESP32-P4 远程编译工作流

统一封装 SSH 远程编译、固件烧录、串口监控和板端测试能力。

## 前置条件

- 远程编译机 `openvela-vm-jul`（Ubuntu, IP `192.168.182.129`）
- 本地 `E:\openvela` 工作区与 VM 通过 SSH key 认证
- 目标板：ESP32-P4 Function EV Board（USB-Serial/JTAG → VM 直通）
- Python ≥ 3.8，已安装 `pyserial`

## 工程级配置

在 `.codex/config.json` 中管理：

```json
{
  "ssh": {
    "host": "openvela-vm-jul",
    "user": "max",
    "identity_file": "~/.ssh/id_ed25519",
    "workspace": "/home/max/openvela-p4-integration"
  },
  "esptool": {
    "chip": "esp32p4",
    "port": "/dev/ttyACM0",
    "baud": 460800,
    "flash_offset": "0x2000",
    "flash_mode": "dio",
    "flash_size": "4MB",
    "flash_freq": "80m"
  },
  "serial": {
    "port": "/dev/ttyACM0",
    "baudrate": 115200,
    "timeout_sec": 1.0
  }
}
```

## 工作流

### 1. 构建固件（build）

```bash
python tools/vm_build.py [--clean]
```

流程：
1. 本地源码上传到 VM：`scp apps/examples/camera_diag/* openvela-vm-jul:/home/max/.../camera_diag/`
2. SSH 进入 VM 执行 `make -j4`（或 `make -j1 V=0` 用于调试）
3. 获取构建产物 SHA-256 和大小

输出规范：
```
BUILD_RC=0 时输出：nuttx.bin SHA-256 <hash>，大小 <bytes>
BUILD_RC≠0 时输出：尾 35 行构建日志和退出码
```

### 2. 烧录固件（flash）

```bash
python tools/vm_flash.py [--offset 0x2000]
```

流程：
1. 确认 `nuttx.bin` 存在且 SHA-256 与构建结果一致
2. 通过 SSH 在 VM 上执行 `esptool.py write-flash`
3. 验证写入（`verify-flash`）

输出规范：
```
烧录成功：Hash of data verified
烧录失败：输出 esptool 错误信息
```

### 3. 串口测试（serial/test）

```bash
python tools/vm_test.py          # 启动 + 基本 NSH 检查
python tools/vm_test_touch.py    # 触摸交互测试
python tools/vm_test_touch2.py   # 触摸坐标校准验证
```

流程：
1. 烧录后等待 10 秒板卡复位
2. Cat `/dev/ttyACM0` 捕获启动日志
3. 发送 NSH 命令：`camera_diag --dsi-ui-live`、`camera_diag --touch-ui-live`
4. 收集串口输出，验证关键日志行

输出规范：
```
成功：NuttShell (NSH) 出现，命令回显正确
失败：输出超时/无回显的具体时间点
```

### 4. 同步并构建（sync + build）

```bash
python tools/vm_sync_and_build.py
```

等价于先后执行 `tools/ 上传 → build`，用于快速迭代。

### 5. 调试（debug）

```bash
python tools/vm_debug.py
```

启动后进入交互模式，可发送自定义 NSH 命令并实时查看串口输出。

## 输出规范

所有工具脚本统一输出格式：

| 状态 | 输出 |
|------|------|
| 成功 | `{"success": true, "exit_code": 0, "stdout": "...", "stderr": ""}` |
| 失败 | `{"success": false, "exit_code": <N>, "stdout": "...", "stderr": "..."}` |
| 超时 | `{"success": false, "error": "timeout after <N> seconds"}` |

## 典型使用示例

```bash
# 完整一轮：构建 → 烧录 → 测试
python tools/vm_build.py
python tools/vm_flash.py
python tools/vm_test.py

# 快速迭代（只改应用层）
python tools/vm_sync_and_build.py
python tools/vm_flash.py
python tools/vm_test_touch.py
```

## 常见问题

1. **`/dev/ttyACM0` 不存在**：检查 VM USB 直通是否生效，执行 `ls /dev/tty*` 确认
2. **烧录失败 `Hash of data not verified`**：降低波特率至 115200 重试
3. **板卡无响应**：按住 BOOT 键再按一下 RST 键后松开 BOOT，进入下载模式
4. **构建失败**：检查 `.config` 中 `CONFIG_EXAMPLES_CAMERA_DIAG` 是否启用