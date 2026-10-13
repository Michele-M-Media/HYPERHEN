#!/usr/bin/env python3
"""Shut down HyperHEN stack on a PS5 from a PC (LAN).

Sequence (daemon side):
  1. stop hyperhen_util.elf
  2. restart SceShellUI (kill → system respawn)
  3. exit hyperhen_daemon.elf

kstuff is intentionally left running — SIGKILL of kstuff panics the kernel.

Wire (TCP port 9048, little-endian):
  request:  u32 magic=0x4F4E494F ('ONIO') + u32 cmd=1 (SHUTDOWN)
  response: 1 byte 0 = accepted

Usage:
  python3 scripts/shutdown_hyperhen.py 192.168.x.x
"""

from __future__ import annotations

import argparse
import socket
import struct
import sys

ONION_CTRL_TCP_PORT = 9048
ONION_CTRL_TCP_MAGIC = 0x4F4E494F  # 'ONIO'
ONION_CTRL_TCP_CMD_SHUTDOWN = 1


def shutdown(host: str, port: int = ONION_CTRL_TCP_PORT, timeout: float = 5.0) -> int:
    frame = struct.pack("<II", ONION_CTRL_TCP_MAGIC, ONION_CTRL_TCP_CMD_SHUTDOWN)
    try:
        with socket.create_connection((host, port), timeout=timeout) as sock:
            sock.sendall(frame)
            reply = sock.recv(1)
    except OSError as e:
        print(f"connect/send failed: {e}", file=sys.stderr)
        print(
            "Check: PS5 and PC on same LAN; HyperHEN daemon running; port 9048 open.",
            file=sys.stderr,
        )
        return 2

    if not reply:
        # Peer may close after accepting without a full byte — treat as success.
        print("shutdown requested (no reply byte; daemon may have exited)")
        return 0
    if reply[0] == 0:
        print("shutdown accepted: util → restart ShellUI → daemon (kstuff remains)")
        return 0
    print(f"daemon rejected frame (byte={reply[0]})", file=sys.stderr)
    return 1


def main() -> None:
    ap = argparse.ArgumentParser(description="Shut down HyperHEN stack on PS5")
    ap.add_argument("ps5_ip", help="PS5 LAN IP address")
    ap.add_argument("--port", type=int, default=ONION_CTRL_TCP_PORT)
    args = ap.parse_args()
    sys.exit(shutdown(args.ps5_ip, args.port))


if __name__ == "__main__":
    main()
