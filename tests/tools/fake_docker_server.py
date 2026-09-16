#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 kontainer developers
# SPDX-License-Identifier: GPL-2.0-or-later
"""最小假 Docker Engine —— 仅用于 UI 开发/验证，不属于产品代码。

用途：在没有 Docker（或不想触碰真实 daemon）的情况下验证 KCM 的
Loading / Empty / Error 状态。只实现一期用到的只读 GET 端点：

    GET /_ping
    GET /version
    GET /info            (以及 /v1.xx/info)
    GET /containers/json (以及 /v1.xx/containers/json)
    GET /images/json     (以及 /v1.xx/images/json)

用法：
    tests/tools/fake_docker_server.py /tmp/fake-docker.sock [--empty] [--api-version 1.56]

    DOCKER_HOST=unix:///tmp/fake-docker.sock kcmshell6 kcm_docker

本脚本只读，绝不修改任何 Docker 状态。
"""

from __future__ import annotations

import argparse
import json
import os
import socketserver
import sys
from http.server import BaseHTTPRequestHandler

CONTAINERS = [
    {
        "Id": "1111111111111111111111111111111111111111111111111111111111111111",
        "Names": ["/fake-running"],
        "Image": "alpine:latest",
        "ImageID": "sha256:aaaa",
        "State": "running",
        "Status": "Up 2 hours",
        "Created": 1789500000,
        "Health": {"Status": "healthy", "FailingStreak": 0},
        "Ports": [{"IP": "0.0.0.0", "PrivatePort": 80, "PublicPort": 8080, "Type": "tcp"}],
    },
    {
        "Id": "2222222222222222222222222222222222222222222222222222222222222222",
        "Names": ["/fake-exited"],
        "Image": "alpine:latest",
        "ImageID": "sha256:aaaa",
        "State": "exited",
        "Status": "Exited (0) 5 minutes ago",
        "Created": 1789500000,
        "Ports": [],
    },
]

IMAGES = [
    {
        "Id": "sha256:aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",
        "RepoTags": ["alpine:latest"],
        "RepoDigests": ["alpine@sha256:bbbb"],
        "Size": 7700000,
        "Created": 1789400000,
        "Containers": 2,
    },
    {
        "Id": "sha256:cccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccccc",
        "RepoTags": None,
        "RepoDigests": None,
        "Size": 1024,
        "Created": 1789400000,
    },
]


def make_handler(empty: bool, api_version: str):
    class Handler(BaseHTTPRequestHandler):
        protocol_version = "HTTP/1.1"

        def _send(self, body: bytes, status: int = 200, content_type: str = "application/json") -> None:
            self.send_response(status)
            self.send_header("Content-Type", content_type)
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)

        def _json(self, payload: object, status: int = 200) -> None:
            self._send(json.dumps(payload).encode(), status)

        def do_GET(self) -> None:  # noqa: N802 (http.server API)
            path = self.path.split("?", 1)[0]
            parts = path.split("/")
            # 去掉可选 /v1.xx 前缀
            if len(parts) > 1 and parts[1].startswith("v1."):
                path = "/" + "/".join(parts[2:])

            if path == "/_ping":
                self._send(b"OK", content_type="text/plain; charset=utf-8")
            elif path == "/version":
                self._json(
                    {
                        "Version": "99.0.0-fake",
                        "ApiVersion": api_version,
                        "MinAPIVersion": "1.40",
                        "Os": "linux",
                        "Arch": "amd64",
                        "Components": [
                            {"Name": "Engine", "Version": "99.0.0-fake", "Details": {"KernelVersion": "0.0.0-fake"}}
                        ],
                    }
                )
            elif path == "/info":
                containers = [] if empty else CONTAINERS
                images = [] if empty else IMAGES
                self._json(
                    {
                        "Name": "fake-engine",
                        "OperatingSystem": "Fake Linux",
                        "OSType": "linux",
                        "Architecture": "x86_64",
                        "KernelVersion": "0.0.0-fake",
                        "CgroupVersion": "2",
                        "Driver": "overlayfs",
                        "Containers": len(containers),
                        "ContainersRunning": sum(1 for c in containers if c["State"] == "running"),
                        "ContainersPaused": 0,
                        "ContainersStopped": sum(1 for c in containers if c["State"] == "exited"),
                        "Images": len(images),
                        "NCPU": 1,
                        "MemTotal": 1024 * 1024 * 1024,
                    }
                )
            elif path == "/containers/json":
                self._json([] if empty else CONTAINERS)
            elif path == "/images/json":
                self._json([] if empty else IMAGES)
            else:
                self._json({"message": f"fake engine has no endpoint {path}"}, status=404)

        def log_message(self, *args: object) -> None:  # 静默
            return

    return Handler


class UnixHTTPServer(socketserver.ThreadingUnixStreamServer):
    allow_reuse_address = True

    def get_request(self):
        request, _ = super().get_request()
        return request, ("localhost", 0)


def main() -> int:
    parser = argparse.ArgumentParser(description="Minimal fake Docker Engine for Kontainer UI testing")
    parser.add_argument("socket", help="unix socket path to listen on")
    parser.add_argument("--empty", action="store_true", help="report zero containers and images")
    parser.add_argument("--api-version", default="1.56", help="API version to advertise (default: 1.56)")
    args = parser.parse_args()

    if os.path.exists(args.socket):
        os.unlink(args.socket)

    handler = make_handler(args.empty, args.api_version)
    with UnixHTTPServer(args.socket, handler) as server:
        os.chmod(args.socket, 0o600)
        print(f"fake docker engine listening on unix://{args.socket}", flush=True)
        try:
            server.serve_forever()
        except KeyboardInterrupt:
            pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
