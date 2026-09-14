#!/usr/bin/env python3
"""Connect to VM and build OpenVela firmware."""

import paramiko
import sys

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

BUILD_CMD = r"""
echo "=== Creating riscv32-unknown-elf shim for ESP toolchain ==="

# ESP toolchain path
ESP_TC=/home/max/.espressif/tools/riscv32-esp-elf/esp-16.1.0_20260609/riscv32-esp-elf/bin

# Create shim directory
mkdir -p /tmp/riscv32-unknown-elf-shim

# Create shim for gcc/g++ (needs arch flags for ESP32-P4)
for tool in gcc g++; do
    cat > /tmp/riscv32-unknown-elf-shim/riscv32-unknown-elf-${tool} << EOF
#!/bin/bash
exec ${ESP_TC}/riscv32-esp-elf-${tool} -march=rv32imac_xesploop_xespv -mabi=ilp32 "\$@"
EOF
    chmod +x /tmp/riscv32-unknown-elf-shim/riscv32-unknown-elf-${tool}
done

# Create shim for other tools (just pass through)
for tool in ld as ar ranlib nm objcopy objdump size strings strip; do
    cat > /tmp/riscv32-unknown-elf-shim/riscv32-unknown-elf-${tool} << EOF
#!/bin/bash
exec ${ESP_TC}/riscv32-esp-elf-${tool} "\$@"
EOF
    chmod +x /tmp/riscv32-unknown-elf-shim/riscv32-unknown-elf-${tool}
done

echo "=== Shim created ==="
ls -la /tmp/riscv32-unknown-elf-shim/

echo "=== Testing shim ==="
/tmp/riscv32-unknown-elf-shim/riscv32-unknown-elf-gcc --version 2>&1 | head -3

echo "=== Now building ==="
cd /home/max/openvela-p4-integration/nuttx

# Set PATH to include shim and genromfs
export PATH=/tmp/riscv32-unknown-elf-shim:/home/max/.local/genromfs/usr/bin:/home/max/.local/bin:$PATH

# Clean and rebuild
make clean 2>&1 | tail -5
make -j1 V=0 2>&1

echo "=== Build result ==="
echo "BUILD_EXIT_CODE=$?"
sha256sum nuttx.bin 2>/dev/null || echo "No nuttx.bin"
wc -c nuttx.bin 2>/dev/null || echo "No nuttx.bin"
"""

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        print("Creating shim and building...")
        stdin, stdout, stderr = client.exec_command(BUILD_CMD, timeout=600)

        # Print stdout in real-time
        for line in iter(stdout.readline, ""):
            print(line, end="")

        # Print stderr
        err = stderr.read().decode()
        if err:
            print("STDERR:", err)

        exit_status = stdout.channel.recv_exit_status()
        print(f"\nCommand exit status: {exit_status}")

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    finally:
        client.close()

    return 0

if __name__ == "__main__":
    sys.exit(main())
