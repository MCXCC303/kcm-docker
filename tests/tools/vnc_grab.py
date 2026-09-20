#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 kcm-docker developers
# SPDX-License-Identifier: GPL-2.0-or-later
"""Minimal VNC (RFB) client: grab the Qt 'vnc' platform plugin's virtual screen to a PNG.

Purpose: render the KCM on a fully isolated virtual screen instead of capturing the
user's desktop.

    QT_QPA_PLATFORM="vnc:port=5999:size=1200x900" kcmshell6 kcm_docker &
    tests/tools/vnc_grab.py 127.0.0.1 5999 out.png
"""

from __future__ import annotations

import socket
import struct
import sys
import time

from PIL import Image


def recv_exact(sock: socket.socket, count: int) -> bytes:
    data = b""
    while len(data) < count:
        chunk = sock.recv(count - len(data))
        if not chunk:
            raise RuntimeError("connection closed by the VNC server")
        data += chunk
    return data


def grab(host: str, port: int, output: str, delay: float) -> None:
    with socket.create_connection((host, port), timeout=15) as sock:
        server_version = recv_exact(sock, 12)
        # Qt's vnc platform plugin speaks RFB 3.3: security type is a 4-byte int, not a list
        try:
            major = int(server_version[4:7])
            minor = int(server_version[8:11])
        except ValueError as error:
            raise RuntimeError(f"unexpected VNC greeting: {server_version!r}") from error

        if (major, minor) >= (3, 7):
            sock.sendall(b"RFB 003.008\n")
            security_count = recv_exact(sock, 1)[0]
            security_types = recv_exact(sock, security_count) if security_count else b""
            if 1 not in security_types:
                raise RuntimeError(f"VNC server requires authentication: {list(security_types)}")
            sock.sendall(b"\x01")  # None
            if struct.unpack(">I", recv_exact(sock, 4))[0] != 0:
                raise RuntimeError("VNC security handshake failed")
        else:
            sock.sendall(b"RFB 003.003\n")
            security_type = struct.unpack(">I", recv_exact(sock, 4))[0]
            if security_type != 1:
                raise RuntimeError(f"unsupported VNC security type {security_type}")
            # RFB 3.3 + None sends no SecurityResult
        sock.sendall(b"\x01")  # shared

        width, height = struct.unpack(">HH", recv_exact(sock, 4))
        pixel_format = recv_exact(sock, 16)
        name_length = struct.unpack(">I", recv_exact(sock, 4))[0]
        recv_exact(sock, name_length)

        bits_per_pixel, depth, big_endian, true_color = struct.unpack(">BBBB", pixel_format[:4])
        red_max, green_max, blue_max = struct.unpack(">HHH", pixel_format[4:10])
        red_shift, green_shift, blue_shift = struct.unpack(">BBB", pixel_format[10:13])
        if not true_color or bits_per_pixel not in (16, 32):
            raise RuntimeError(f"unsupported pixel format: bpp={bits_per_pixel} trueColor={true_color}")

        # Raw encoding only
        sock.sendall(struct.pack(">BBHI", 2, 0, 1, 0))
        # Let the UI finish rendering before the first grab
        time.sleep(delay)
        sock.sendall(struct.pack(">BBHHHH", 3, 0, 0, 0, width, height))

        while True:
            message_type = recv_exact(sock, 1)[0]
            if message_type == 0:  # FramebufferUpdate
                break
            if message_type == 1:  # SetColourMapEntries
                recv_exact(sock, 3)
                count = struct.unpack(">H", recv_exact(sock, 2))[0]
                recv_exact(sock, count * 6)
                continue
            if message_type == 2:  # Bell
                continue
            if message_type == 3:  # ServerCutText
                recv_exact(sock, 3)
                length = struct.unpack(">I", recv_exact(sock, 4))[0]
                recv_exact(sock, length)
                continue
            raise RuntimeError(f"unexpected RFB message {message_type}")

        recv_exact(sock, 1)  # padding
        rect_count = struct.unpack(">H", recv_exact(sock, 2))[0]

        image = Image.new("RGB", (width, height))
        for _ in range(rect_count):
            x, y, rect_width, rect_height, encoding = struct.unpack(">HHHHi", recv_exact(sock, 12))
            if encoding != 0:
                raise RuntimeError(f"unsupported encoding {encoding}")
            raw = recv_exact(sock, rect_width * rect_height * (bits_per_pixel // 8))
            pixels = Image.frombytes(
                "RGBA" if bits_per_pixel == 32 else "RGB",
                (rect_width, rect_height),
                raw,
                "raw",
                "BGRA" if bits_per_pixel == 32 else "BGR",
            )
            image.paste(pixels.convert("RGB"), (x, y))

        image.save(output)
        print(f"saved {output} ({width}x{height}, {bits_per_pixel}bpp)")


if __name__ == "__main__":
    host = sys.argv[1] if len(sys.argv) > 1 else "127.0.0.1"
    vnc_port = int(sys.argv[2]) if len(sys.argv) > 2 else 5999
    out = sys.argv[3] if len(sys.argv) > 3 else "vnc.png"
    wait = float(sys.argv[4]) if len(sys.argv) > 4 else 3.0
    grab(host, vnc_port, out, wait)
