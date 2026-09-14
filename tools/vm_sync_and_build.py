#!/usr/bin/env python3
"""Sync local file to VM and build."""

import paramiko
import sys
import os

VM_HOST = "192.168.182.129"
VM_USER = "max"
VM_PASS = "110110110Gaj"

LOCAL_FILE = r"E:\openvela\.codex\camera_diag-energy-level\esp32p4_dsi.c"
REMOTE_FILE = "/home/max/openvela-p4-integration/apps/examples/camera_diag/esp32p4_dsi.c"

def main():
    client = paramiko.SSHClient()
    client.set_missing_host_key_policy(paramiko.AutoAddPolicy())

    try:
        print(f"Connecting to {VM_HOST}...")
        client.connect(VM_HOST, username=VM_USER, password=VM_PASS, timeout=10)
        print("Connected!")

        # Upload the modified file
        print(f"Uploading {LOCAL_FILE} to {REMOTE_FILE}...")
        sftp = client.open_sftp()
        sftp.put(LOCAL_FILE, REMOTE_FILE)
        sftp.close()
        print("Upload complete!")

        # Verify the upload
        print("Verifying upload...")
        stdin, stdout, stderr = client.exec_command(f"grep 'shared_irq=' {REMOTE_FILE}")
        output = stdout.read().decode()
        if "shared_irq=" in output:
            print("OK: File contains our changes")
        else:
            print("WARNING: File may not contain our changes")
            print(output)

    except Exception as e:
        print(f"Error: {e}", file=sys.stderr)
        return 1
    finally:
        client.close()

    return 0

if __name__ == "__main__":
    sys.exit(main())
