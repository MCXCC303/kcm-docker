#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 kontainer developers
# SPDX-License-Identifier: GPL-2.0-or-later
"""最小假 Docker Engine —— 仅用于 UI 开发/验证，不属于产品代码。

用途：在没有 Docker（或不想触碰真实 daemon）的情况下验证 KCM 的
Loading / Empty / Error 状态，以及四期的写操作界面（启动 / 停止 / 重启 / 删除、
镜像拉取与删除）。实现的端点：

    GET    /_ping
    GET    /version
    GET    /info                  (以及 /v1.xx/info)
    GET    /containers/json
    GET    /images/json
    GET    /system/df
    GET    /containers/{id}/json
    GET    /containers/{id}/stats
    POST   /containers/{id}/start | stop | restart
    DELETE /containers/{id}
    POST   /images/create         (chunked 进度流)
    DELETE /images/{name}

用法：
    tests/tools/fake_docker_server.py /tmp/fake-docker.sock [--empty] [--api-version 1.56]

    DOCKER_HOST=unix:///tmp/fake-docker.sock kcmshell6 kcm_docker

状态只存在于**本进程内存**里（启动 / 删除会真的改变列表，便于观察界面刷新），
绝不触碰任何真实 Docker 资源。socket 权限是 0600，因此写权限门会放行。
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


PULL_LINES = [
    {"status": "Pulling from library/hello-world"},
    {"status": "Downloading", "progressDetail": {"current": 1000, "total": 4000}, "id": "aaa"},
    {"status": "Downloading", "progressDetail": {"current": 4000, "total": 4000}, "id": "aaa"},
    {"status": "Pull complete", "id": "aaa"},
    {"status": "Downloading", "progressDetail": {"current": 500, "total": 2000}, "id": "bbb"},
    {"status": "Pull complete", "id": "bbb"},
    {"status": "Status: Downloaded newer image for hello-world:latest"},
]


def make_handler(empty: bool, api_version: str):
    # 每个进程一份内存状态：写操作真的改动它，界面因此能看到刷新
    containers = [] if empty else [dict(entry) for entry in CONTAINERS]
    images = [] if empty else [dict(entry) for entry in IMAGES]

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
                # 注意：这里不要重新绑定 containers / images，
                # 否则 do_GET 内部会把它们当成局部变量（UnboundLocalError）；
                # 计数直接取内存态，写操作之后界面上的数字才会跟着变
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
                self._json(containers)
            elif path == "/images/json":
                self._json(images)
            else:
                self._json({"message": f"fake engine has no endpoint {path}"}, status=404)

        def do_POST(self) -> None:  # noqa: N802 (http.server API)
            path, _, query = self.path.partition("?")
            parts = path.split("/")
            if len(parts) > 1 and parts[1].startswith("v1."):
                path = "/" + "/".join(parts[2:])

            if path == "/images/create":
                self._pull_stream(query)
                return

            if path.startswith("/containers/"):
                container = self._find_container(path.split("/")[2])
                if container is None:
                    self._json({"message": "No such container"}, status=404)
                    return
                if path.endswith("/start"):
                    container["State"] = "running"
                    container["Status"] = "Up 1 second"
                    self._send(b"", status=204)
                    return
                if path.endswith("/stop"):
                    container["State"] = "exited"
                    container["Status"] = "Exited (0) 1 second ago"
                    self._send(b"", status=204)
                    return
                if path.endswith("/restart"):
                    container["State"] = "running"
                    container["Status"] = "Up 1 second"
                    self._send(b"", status=204)
                    return

            self._json({"message": f"fake engine has no endpoint {path}"}, status=404)

        def do_DELETE(self) -> None:  # noqa: N802 (http.server API)
            path, _, query = self.path.partition("?")
            parts = path.split("/")
            if len(parts) > 1 and parts[1].startswith("v1."):
                path = "/" + "/".join(parts[2:])

            if path.startswith("/containers/"):
                container = self._find_container(path.split("/")[2])
                if container is None:
                    self._json({"message": "No such container"}, status=404)
                    return
                if container["State"] == "running" and "force=true" not in query:
                    self._json({"message": "You cannot remove a running container"}, status=409)
                    return
                containers.remove(container)
                self._send(b"", status=204)
                return

            if path.startswith("/images/"):
                name = "/".join(path.split("/")[2:])
                for image in list(images):
                    tags = image.get("RepoTags") or []
                    if image["Id"] in name or image["Id"].replace("sha256:", "").startswith(name) or name in tags:
                        images.remove(image)
                        self._json([{"Deleted": image["Id"]}])
                        return
                self._json({"message": "No such image"}, status=404)
                return

            self._json({"message": f"fake engine has no endpoint {path}"}, status=404)

        @staticmethod
        def _find_container(identifier: str):
            for container in containers:
                if container["Id"] == identifier or container["Id"].startswith(identifier):
                    return container
            return None

        def _pull_stream(self, query: str) -> None:
            """chunked 逐行推送拉取进度（与真实 daemon 的形态一致）。"""
            self.send_response(200)
            self.send_header("Content-Type", "application/json")
            self.send_header("Transfer-Encoding", "chunked")
            self.end_headers()
            for line in PULL_LINES:
                payload = (json.dumps(line) + "\n").encode()
                self.wfile.write(b"%x\r\n" % len(payload) + payload + b"\r\n")
                self.wfile.flush()
            self.wfile.write(b"0\r\n\r\n")
            # 拉取成功后镜像列表里真的多一个（界面刷新就能看到）
            images.append(
                {
                    "Id": "sha256:dddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddddd",
                    "RepoTags": ["hello-world:latest"],
                    "RepoDigests": ["hello-world@sha256:eeee"],
                    "Size": 20000,
                    "Created": 1789500000,
                    "Containers": 0,
                }
            )

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
