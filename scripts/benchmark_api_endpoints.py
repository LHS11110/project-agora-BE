#!/usr/bin/env python3
"""External client-side Agora HTTP endpoint benchmark.

This script adds no timing code to Spring or C++. It consumes each complete HTTP
response body. WebSocket handshakes are measured by the separate
benchmark_websocket_routes.py client on the shared Docker network. This script
creates uniquely named temporary users/canvases and removes those fixtures.
"""
from __future__ import annotations

import argparse
import base64
import csv
import json
import math
import os
import secrets
import socket
import ssl
import statistics
import subprocess
import time
from collections import Counter, defaultdict
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote, urlencode, urlparse

import requests

ROOT = Path(__file__).resolve().parents[1]
DB_ENV = ROOT.parent / "project-agora-DB" / "mssql" / ".env"


def load_env(path: Path) -> dict[str, str]:
    values: dict[str, str] = {}
    for line in path.read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        values[key.strip()] = value.strip().strip('"').strip("'")
    return values


def json_bytes(value: object) -> bytes:
    return json.dumps(value, ensure_ascii=False, separators=(",", ":")).encode("utf-8")


def percentile(values: list[float], p: float) -> float:
    ordered = sorted(values)
    return ordered[max(0, math.ceil(p * len(ordered)) - 1)]


class Benchmark:
    def __init__(self, spring: str, cpp: str):
        if urlparse(spring).scheme != "https" or urlparse(cpp).scheme != "https":
            raise ValueError("Benchmark endpoints require HTTPS")
        self.spring = spring.rstrip("/")
        self.cpp = cpp.rstrip("/")
        self.spring_session = requests.Session()
        self.cpp_session = requests.Session()
        self.ca = os.environ.get("SERVICE_TLS_CA", str(ROOT.parent / "project-agora-FE/.local-https/root-ca.pem"))
        self.spring_session.verify = self.ca
        self.cpp_session.verify = self.ca
        self.samples: dict[tuple[str, str], list[dict[str, object]]] = defaultdict(list)
        self.route_samples: Counter[str] = Counter()
        self.started = time.perf_counter()

    def http(self, name: str, method: str, base: str, path: str, *, body: bytes | None = None,
             params: dict[str, str] | None = None, headers: dict[str, str] | None = None,
             size_label: str = "fixed", timeout: tuple[float, float] = (5, 60),
             session: requests.Session | None = None) -> requests.Response | None:
        client = session or (self.cpp_session if base == self.cpp else self.spring_session)
        query_bytes = len(urlencode(params).encode("ascii")) if params else 0
        body_bytes = len(body) if body else 0
        request_bytes = query_bytes + body_bytes
        start = time.perf_counter_ns()
        response = None
        status: int | str = "ERR"
        response_bytes = 0
        try:
            response = client.request(method, base + path, data=body, params=params,
                                      headers=headers, timeout=timeout)
            response_bytes = len(response.content)  # requests buffers and fully consumes the body.
            status = response.status_code
        except requests.RequestException:
            pass
        elapsed_ms = (time.perf_counter_ns() - start) / 1_000_000
        self.samples[(name, size_label)].append({
            "ms": elapsed_ms, "request_bytes": request_bytes,
            "body_bytes": body_bytes, "query_bytes": query_bytes,
            "response_bytes": response_bytes, "status": status,
        })
        self.route_samples[name] += 1
        return response

    def ws(self, name: str, route: str, token: str, expected_type: str,
           size_label: str = "fixed") -> bool:
        target = route + "?token=" + quote(token, safe=".")
        request = (
            f"GET {target} HTTP/1.1\r\n"
            "Host: 127.0.0.1:8002\r\n"
            "Upgrade: websocket\r\n"
            "Connection: Upgrade\r\n"
            f"Sec-WebSocket-Key: {base64.b64encode(secrets.token_bytes(16)).decode()}\r\n"
            "Sec-WebSocket-Version: 13\r\n\r\n"
        ).encode("ascii")
        started = time.perf_counter_ns()
        status: int | str = "ERR"
        response_size = 0
        message = b""
        sock = None
        try:
            host = urlparse(self.cpp).hostname
            sock = ssl.create_default_context(cafile=self.ca).wrap_socket(socket.create_connection((host, 8002), timeout=10), server_hostname=host)
            sock.settimeout(10)
            sock.sendall(request)
            buf = bytearray()
            while b"\r\n\r\n" not in buf:
                piece = sock.recv(4096)
                if not piece:
                    break
                buf.extend(piece)
                if len(buf) > 32768:
                    break
            raw = bytes(buf)
            marker = raw.find(b"\r\n\r\n")
            if marker >= 0:
                head, pending = raw[:marker + 4], raw[marker + 4:]
                first = head.split(b"\r\n", 1)[0].split()
                status = int(first[1]) if len(first) > 1 and first[1].isdigit() else "ERR"
                response_size = len(head)
                if status == 101:
                    frame = _read_ws_frame(sock, pending)
                    message = frame[1]
                    response_size += frame[2]
                    # Complete close handshake enough for the server to observe a clean close.
                    mask = secrets.token_bytes(4)
                    close_payload = b"\x03\xe8"
                    masked = bytes(b ^ mask[i % 4] for i, b in enumerate(close_payload))
                    sock.sendall(bytes((0x88, 0x80 | len(close_payload))) + mask + masked)
            text = message.decode("utf-8", "replace") if message else ""
            try:
                ok = status == 101 and json.loads(text).get("type") == expected_type
            except (ValueError, AttributeError):
                ok = False
        except (OSError, ValueError, IndexError):
            ok = False
        finally:
            if sock is not None:
                try:
                    sock.close()
                except OSError:
                    pass
        elapsed_ms = (time.perf_counter_ns() - started) / 1_000_000
        self.samples[(name, size_label)].append({
            "ms": elapsed_ms, "request_bytes": len(target.encode("ascii")),
            "body_bytes": 0, "query_bytes": len(target.encode("ascii")),
            "response_bytes": response_size, "status": status,
        })
        self.route_samples[name] += 1
        return ok

    def rows(self) -> list[dict[str, object]]:
        result = []
        for (name, size_label), entries in sorted(self.samples.items()):
            times = [float(x["ms"]) for x in entries]
            statuses = Counter(str(x["status"]) for x in entries)
            req = [int(x["request_bytes"]) for x in entries]
            body = [int(x["body_bytes"]) for x in entries]
            query = [int(x["query_bytes"]) for x in entries]
            resp = [int(x["response_bytes"]) for x in entries]
            result.append({
                "endpoint": name, "size_group": size_label, "samples": len(entries),
                "success_2xx_or_101": sum(v for k, v in statuses.items()
                                           if k.isdigit() and (200 <= int(k) < 300 or int(k) == 101)),
                "statuses": dict(statuses),
                "mean_ms": statistics.fmean(times), "p50_ms": percentile(times, .50),
                "p95_ms": percentile(times, .95), "min_ms": min(times), "max_ms": max(times),
                "avg_request_data_bytes": statistics.fmean(req),
                "min_request_data_bytes": min(req), "max_request_data_bytes": max(req),
                "avg_body_bytes": statistics.fmean(body), "avg_query_bytes": statistics.fmean(query),
                "avg_response_bytes": statistics.fmean(resp),
            })
        return result


def _read_ws_frame(sock: socket.socket, pending: bytes) -> tuple[int, bytes, int]:
    buf = bytearray(pending)
    total_frame_bytes = 0
    def exact(n: int) -> bytes:
        while len(buf) < n:
            part = sock.recv(max(4096, n - len(buf)))
            if not part:
                raise OSError("websocket closed")
            buf.extend(part)
        value = bytes(buf[:n])
        del buf[:n]
        return value
    while True:
        first, second = exact(2)
        total_frame_bytes += 2
        opcode = first & 0x0F
        length = second & 0x7F
        if length == 126:
            length = int.from_bytes(exact(2), "big")
            total_frame_bytes += 2
        elif length == 127:
            length = int.from_bytes(exact(8), "big")
            total_frame_bytes += 8
        mask = exact(4) if second & 0x80 else b""
        total_frame_bytes += len(mask)
        payload = exact(length)
        total_frame_bytes += length
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        if opcode == 0x9:
            pong_mask = secrets.token_bytes(4)
            pong = bytes(b ^ pong_mask[i % 4] for i, b in enumerate(payload))
            sock.sendall(bytes((0x8A, 0x80 | len(payload))) + pong_mask + pong)
            continue
        if opcode == 0x8:
            raise OSError("websocket closed before data frame")
        return opcode, payload, total_frame_bytes


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--iterations", type=int, default=1000,
                        help="samples per endpoint (mutating create/delete pairs use 2x this count)")
    parser.add_argument("--spring", default="https://localhost:8443")
    parser.add_argument("--cpp", default="https://agora-cpp:8000")
    parser.add_argument("--output", default="/tmp/agora-api-benchmark.json")
    args = parser.parse_args()
    n = args.iterations
    if n < 1:
        parser.error("--iterations must be positive")
    be = load_env(ROOT / ".env")
    db = load_env(DB_ENV)
    run_id = datetime.now(timezone.utc).strftime("%y%m%d%H%M%S") + secrets.token_hex(3)
    user_prefix = f"bench-{run_id}-"
    bench = Benchmark(args.spring, args.cpp)
    spring_headers = {"Authorization": "Bearer "}
    cpp_headers = {"X-Agora-Internal-Token": be["CPP_INTERNAL_API_TOKEN"]}
    admin_email = "admin@agora.com"
    admin_password = be["ADMIN_PASSWORD"]
    canvas_ids: list[int] = []
    accounts: list[dict[str, object]] = []
    fixture_id: int | None = None
    canvas_access_token = ""
    canvas_password = f"bench-{run_id}-canvas-password"
    cleanup_sql_ok = False

    def encode_auth_body(value: object) -> bytes:
        return json_bytes(value)

    def hit(name: str, method: str, path: str, *, body: bytes | None = None,
            params: dict[str, str] | None = None, content_type: str | None = None,
            size_label: str = "fixed", session: requests.Session | None = None,
            cpp: bool = False) -> requests.Response | None:
        base = bench.cpp if cpp else bench.spring
        headers = dict(cpp_headers if cpp else {})
        if content_type:
            headers["Content-Type"] = content_type
        if cpp:
            headers.update(cpp_headers)
        return bench.http(name, method, base, path, body=body, params=params,
                          headers=headers or None, size_label=size_label, session=session)

    def run_fixed(name: str, method: str, path: str, repetitions: int = n, *,
                  body_factory=None, params_factory=None, content_type: str | None = None,
                  size_label: str = "fixed", cpp: bool = False,
                  callback=None, max_consecutive_failures: int = 5) -> None:
        failures = 0
        for i in range(repetitions):
            body = body_factory(i) if body_factory else None
            params = params_factory(i) if params_factory else None
            label = size_label(i) if callable(size_label) else size_label
            response = hit(name, method, path, body=body, params=params,
                           content_type=content_type, size_label=label, cpp=cpp)
            if response is not None and 200 <= response.status_code < 300:
                failures = 0
                if callback:
                    callback(i, response)
            else:
                failures += 1
                if failures >= max_consecutive_failures:
                    print(f"stopped early after repeated failures: {name}")
                    break
        show_progress(name)

    def show_progress(name: str) -> None:
        entries = [x for (key, _), values in bench.samples.items() if key == name for x in values]
        if not entries:
            print(f"done {name}: no samples")
            return
        ms = statistics.fmean(float(x["ms"]) for x in entries)
        statuses = Counter(str(x["status"]) for x in entries)
        print(f"done {name}: n={len(entries)} mean={ms:.2f}ms status={dict(statuses)}")

    # Verify the admin token once before starting the benchmark; this warm-up is not sampled.
    login = bench.spring_session.post(bench.spring + "/api/auth/login",
        data=encode_auth_body({"email": admin_email, "password": admin_password}),
        headers={"Content-Type": "application/json"}, timeout=(5, 60))
    if login.status_code != 200:
        raise RuntimeError(f"Admin preflight login failed ({login.status_code}); no benchmark data was created")
    admin_token = login.json().get("accessToken", "")
    if not admin_token:
        raise RuntimeError("Admin preflight did not return a login token")
    bench.spring_session.headers.update({"Authorization": "Bearer " + admin_token})

    try:
        print(f"benchmark run={run_id} samples_per_endpoint={n}; target=local Docker; secrets and bodies are not written")

        # Authentication and read-only Spring routes.
        run_fixed("Spring GET /api/auth/health", "GET", "/api/auth/health")
        login_body = encode_auth_body({"email": admin_email, "password": admin_password})
        run_fixed("Spring POST /api/auth/login", "POST", "/api/auth/login",
                  body_factory=lambda _: login_body, content_type="application/json")

        # One temporary user is created as sample 1, then the route is completed after read APIs.
        account0_email = f"{user_prefix}auth-0000@example.invalid"
        account0_nick = f"agoraBench{run_id}a0000"
        account0_body = encode_auth_body({"email": account0_email,
                                           "password": "BenchPass123!",
                                           "nickname": account0_nick})
        first_user = hit("Spring POST /api/auth/signup", "POST", "/api/auth/signup",
                         body=account0_body, content_type="application/json")
        if first_user is None or first_user.status_code != 201:
            raise RuntimeError(f"Temporary fixture user creation failed ({getattr(first_user, 'status_code', 'network')})")
        account0 = first_user.json()
        account0.update({"email_for_cleanup": account0_email, "password_for_cleanup": "BenchPass123!"})
        accounts.append(account0)
        run_fixed("Spring POST /api/auth/me", "POST", "/api/auth/me",
                  body_factory=lambda _: encode_auth_body({"token": admin_token}),
                  content_type="application/json")
        run_fixed("Spring GET /api/auth/users", "GET", "/api/auth/users")
        run_fixed("Spring GET /api/users (admin)", "GET", "/api/users")
        run_fixed("Spring GET /api/users/{nickname}/{tagNumber}", "GET",
                  f"/api/users/{quote(str(account0['nickname']), safe='')}/{account0['tag_number']}")
        run_fixed("Spring GET /api/test/cpp-active-canvases", "GET", "/api/test/cpp-active-canvases")
        run_fixed("Spring GET /api/test/cpp-canvas-count", "GET", "/api/test/cpp-canvas-count")

        # Load-balancer and database address endpoints.
        for method in ("GET", "POST"):
            run_fixed(f"Spring {method} /api/load-balancer/allocate/server", method,
                      "/api/load-balancer/allocate/server")
            run_fixed(f"Spring {method} /api/load-balancer/allocate/redis", method,
                      "/api/load-balancer/allocate/redis")
        run_fixed("Spring GET /api/load-balancer/database", "GET", "/api/load-balancer/database")
        run_fixed("Spring POST /api/load-balancer/allocate/database", "POST",
                  "/api/load-balancer/allocate/database")
        run_fixed("Spring GET /api/database/address", "GET", "/api/database/address")

        # One canvas fixture supports safe read and settings endpoints.
        desc_lengths = (0, 256, 1024, 4000)
        def canvas_json_body(i: int) -> bytes:
            desc_len = desc_lengths[i % len(desc_lengths)]
            return encode_auth_body({"canvasName": f"bench{run_id}j{i:04d}",
                                    "description": "d" * desc_len})
        first_canvas = hit("Spring POST /api/canvases [JSON]", "POST", "/api/canvases",
                           body=canvas_json_body(0), content_type="application/json",
                           size_label="description=0")
        if first_canvas is None or first_canvas.status_code != 201:
            raise RuntimeError(f"Temporary fixture canvas creation failed ({getattr(first_canvas, 'status_code', 'network')})")
        first_canvas_doc = first_canvas.json()
        fixture_id = int(first_canvas_doc["canvas_id"])
        canvas_ids.append(fixture_id)

        query_sizes = (0, 64, 512, 2048)
        def search_params(i: int) -> dict[str, str] | None:
            size = query_sizes[i % len(query_sizes)]
            return None if size == 0 else {"name": "x" * size}
        def search_label(i: int) -> str:
            return f"query={query_sizes[i % len(query_sizes)]}"
        run_fixed("Spring GET /api/canvases", "GET", "/api/canvases", params_factory=search_params,
                  size_label=search_label)
        run_fixed("Spring GET /api/canvases/search", "GET", "/api/canvases/search",
                  params_factory=search_params, size_label=search_label)
        run_fixed("Spring GET /api/canvases/{canvasId}", "GET", f"/api/canvases/{fixture_id}")
        run_fixed("Spring GET /api/canvases/{canvasId}/image", "GET",
                  f"/api/canvases/{fixture_id}/image")
        run_fixed("Spring GET /api/canvases/{canvasId}/settings", "GET",
                  f"/api/canvases/{fixture_id}/settings")

        # First JSON create is included; create the remaining n-1 after listing endpoints.
        def create_json_callback(i: int, response: requests.Response) -> None:
            if i == 0:
                return
            canvas_ids.append(int(response.json()["canvas_id"]))
        for i in range(1, n):
            label = f"description={desc_lengths[i % len(desc_lengths)]}"
            response = hit("Spring POST /api/canvases [JSON]", "POST", "/api/canvases",
                           body=canvas_json_body(i), content_type="application/json", size_label=label)
            if response is not None and response.status_code == 201:
                canvas_ids.append(int(response.json()["canvas_id"]))
            elif i % 100 == 0:
                print(f"JSON canvas creation progress: {i}/{n-1}")
        show_progress("Spring POST /api/canvases [JSON]")

        # Multipart route, with 4 valid description sizes; no image means default image handling.
        boundary = "AgoraBench" + run_id
        def multipart_canvas(i: int) -> bytes:
            desc_len = desc_lengths[i % len(desc_lengths)]
            values = (("canvasName", f"bench{run_id}m{i:04d}"), ("description", "d" * desc_len))
            chunks = []
            for key, value in values:
                chunks.append((f"--{boundary}\r\nContent-Disposition: form-data; name=\"{key}\"\r\n"
                               "Content-Type: text/plain; charset=UTF-8\r\n\r\n" + value + "\r\n").encode("utf-8"))
            chunks.append(f"--{boundary}--\r\n".encode("ascii"))
            return b"".join(chunks)
        for i in range(n):
            label = f"description={desc_lengths[i % len(desc_lengths)]}"
            response = hit("Spring POST /api/canvases [multipart]", "POST", "/api/canvases",
                           body=multipart_canvas(i),
                           content_type=f"multipart/form-data; boundary={boundary}", size_label=label)
            if response is not None and response.status_code == 201:
                canvas_ids.append(int(response.json()["canvas_id"]))
            elif i % 100 == 0:
                print(f"multipart canvas creation progress: {i}/{n}")
        show_progress("Spring POST /api/canvases [multipart]")

        # Canvas setting changes: all happen on the inactive fixture, with valid payload sizes.
        def name_body(i: int) -> bytes:
            length = (16, 64, 128, 255)[i % 4]
            value = (f"n{i:04d}" + "x" * 255)[:length]
            return encode_auth_body({"canvas_name": value})
        run_fixed("Spring PATCH /api/canvases/{canvasId}/name", "PATCH",
                  f"/api/canvases/{fixture_id}/name", body_factory=name_body,
                  content_type="application/json", size_label=lambda i: f"name={ (16,64,128,255)[i%4] }")

        def desc_body(i: int) -> bytes:
            length = desc_lengths[i % 4]
            return encode_auth_body({"description": "d" * length})
        run_fixed("Spring PATCH /api/canvases/{canvasId}/description", "PATCH",
                  f"/api/canvases/{fixture_id}/description", body_factory=desc_body,
                  content_type="application/json", size_label=lambda i: f"description={desc_lengths[i%4]}")

        def pass_body(i: int) -> bytes:
            length = (8, 24, 48, 64)[i % 4]
            return encode_auth_body({"canvas_password": "p" * length})
        run_fixed("Spring PATCH /api/canvases/{canvasId}/password", "PATCH",
                  f"/api/canvases/{fixture_id}/password", body_factory=pass_body,
                  content_type="application/json", size_label=lambda i: f"password={ (8,24,48,64)[i%4] }")
        canvas_password = "p" * 64

        # User-create endpoints: 1000 successful API-created rows per route.
        for i in range(1, n):
            email = f"{user_prefix}auth-{i:04d}@example.invalid"
            nickname = f"agoraBench{run_id}a{i:04d}"
            body = encode_auth_body({"email": email, "password": "BenchPass123!", "nickname": nickname})
            response = hit("Spring POST /api/auth/signup", "POST", "/api/auth/signup",
                           body=body, content_type="application/json")
            if response is not None and response.status_code == 201:
                item = response.json(); item.update({"email_for_cleanup": email, "password_for_cleanup": "BenchPass123!"})
                accounts.append(item)
        show_progress("Spring POST /api/auth/signup")

        user_accounts: list[dict[str, object]] = []
        for i in range(n):
            email = f"{user_prefix}alias-{i:04d}@example.invalid"
            nickname = f"agoraBench{run_id}u{i:04d}"
            body = encode_auth_body({"email": email, "password": "BenchPass123!", "nickname": nickname})
            response = hit("Spring POST /api/users", "POST", "/api/users", body=body,
                           content_type="application/json")
            if response is not None and response.status_code == 201:
                item = response.json(); item.update({"email_for_cleanup": email, "password_for_cleanup": "BenchPass123!"})
                user_accounts.append(item); accounts.append(item)
        show_progress("Spring POST /api/users")

        # User GET/PUT/PATCH use account0 and the admin token, leaving existing users untouched.
        current_user = accounts[0]
        for method in ("PUT", "PATCH"):
            name = f"Spring {method} /api/users/{{nickname}}/{{tagNumber}}"
            for i in range(n):
                previous_nick = str(current_user["nickname"])
                previous_tag = int(current_user["tag_number"])
                new_nick = f"bench{run_id}{method.lower()}{i:04d}"
                body = encode_auth_body({"nickname": new_nick})
                response = hit(name, method,
                               f"/api/users/{quote(previous_nick, safe='')}/{previous_tag}",
                               body=body, content_type="application/json")
                if response is not None and response.status_code == 200:
                    updated = response.json()
                    current_user["nickname"] = updated.get("nickname", new_nick)
                    current_user["tag_number"] = updated.get("tag_number", previous_tag)
            show_progress(name)
        accounts[0]["nickname"] = current_user["nickname"]
        accounts[0]["tag_number"] = current_user["tag_number"]

        # Access is an authenticated endpoint that returns a short-lived WebSocket token.
        run_fixed("Spring POST /api/canvases/{canvasId}/access", "POST",
                  f"/api/canvases/{fixture_id}/access",
                  body_factory=lambda _: encode_auth_body({"password": canvas_password}),
                  content_type="application/json")

        # Participant routes mutate only the disposable canvas and remove each test account afterwards.
        participant_accounts = user_accounts[:n]
        for account in participant_accounts:
            body = encode_auth_body({"nickname": account["nickname"], "tag_number": account["tag_number"]})
            hit("Spring POST /api/canvases/{canvasId}/people", "POST",
                f"/api/canvases/{fixture_id}/people", body=body, content_type="application/json")
        show_progress("Spring POST /api/canvases/{canvasId}/people")
        for account in participant_accounts:
            body = encode_auth_body({"nickname": account["nickname"], "tag_number": account["tag_number"]})
            hit("Spring DELETE /api/canvases/{canvasId}/people", "DELETE",
                f"/api/canvases/{fixture_id}/people", body=body, content_type="application/json")
        show_progress("Spring DELETE /api/canvases/{canvasId}/people")

        # C++ public health and all protected REST control routes. Fake positive IDs avoid changing live sessions.
        run_fixed("C++ GET /health", "GET", "/health", cpp=True)
        run_fixed("C++ GET /", "GET", "/", cpp=True)
        run_fixed("C++ POST /api/users/{userId}/disconnect", "POST",
                  "/api/users/2147483000/disconnect", cpp=True)
        run_fixed("C++ POST /api/canvas/{canvasId}/users/{userId}/disconnect", "POST",
                  "/api/canvas/2147483000/users/2147483000/disconnect", cpp=True)
        run_fixed("C++ DELETE /api/canvas/{canvasId}", "DELETE",
                  "/api/canvas/2147483000", cpp=True)
        run_fixed("C++ GET /api/canvas/count", "GET", "/api/canvas/count", cpp=True)
        run_fixed("C++ GET /api/canvas/active", "GET", "/api/canvas/active", cpp=True)

        # Delete every successful canvas create, collecting ~2n real delete samples.
        for canvas_id in list(canvas_ids):
            response = hit("Spring DELETE /api/canvases/{canvasId}", "DELETE",
                           f"/api/canvases/{canvas_id}")
            if response is not None and response.status_code == 204:
                canvas_ids.remove(canvas_id)
        show_progress("Spring DELETE /api/canvases/{canvasId}")

        # Delete every API-created account (soft delete API), then hard-remove only this run's exact disposable rows below.
        for item in list(accounts):
            nick = quote(str(item["nickname"]), safe="")
            tag = int(item["tag_number"])
            hit("Spring DELETE /api/users/{nickname}/{tagNumber}", "DELETE",
                f"/api/users/{nick}/{tag}")
        show_progress("Spring DELETE /api/users/{nickname}/{tagNumber}")

    finally:
        # Retry Spring cleanup for any fixtures not already removed by the measured delete loops.
        for canvas_id in list(canvas_ids):
            try:
                cleanup_response = hit("Cleanup Spring canvas", "DELETE", f"/api/canvases/{canvas_id}")
                if cleanup_response is not None and cleanup_response.status_code == 204:
                    canvas_ids.remove(canvas_id)
            except Exception:
                pass
        # Hard-delete only accounts with this run's unique email prefix, including their new session rows.
        try:
            table = db.get("MSSQL_TABLE_USERS", "users")
            if not table.replace("_", "").isalnum():
                raise ValueError("invalid configured user table name")
            pattern = user_prefix + "%@example.invalid"
            sql = (
                "SET QUOTED_IDENTIFIER ON; SET ANSI_NULLS ON; SET NOCOUNT ON; BEGIN TRY BEGIN TRANSACTION; "
                f"DELETE s FROM dbo.[user_sessions] s JOIN dbo.[{table}] u ON u.user_id=s.user_id WHERE u.email LIKE N'{pattern}'; "
                f"DELETE u FROM dbo.[{table}] u WHERE u.email LIKE N'{pattern}'; "
                "COMMIT TRANSACTION; SELECT COUNT(*) AS remaining FROM dbo.[" + table + "] WHERE email LIKE N'" + pattern + "'; "
                "END TRY BEGIN CATCH IF @@TRANCOUNT > 0 ROLLBACK TRANSACTION; THROW; END CATCH;"
            )
            db_env = os.environ.copy()
            db_env["SQLCMDPASSWORD"] = db["MSSQL_SA_PASSWORD"]
            sqlcmd = "/opt/mssql-tools18/bin/sqlcmd"
            result = subprocess.run(
                ["docker", "exec", "-e", "SQLCMDPASSWORD", "agora-mssql", sqlcmd,
                 "-S", "localhost", "-U", "sa", "-C", "-I", "-d", db["MSSQL_DB"],
                 "-h", "-1", "-W", "-b", "-Q", sql],
                text=True, capture_output=True, env=db_env, timeout=90)
            remaining_lines = [line.strip() for line in result.stdout.splitlines() if line.strip()]
            cleanup_sql_ok = result.returncode == 0 and bool(remaining_lines) and remaining_lines[-1] == "0"
            if not cleanup_sql_ok:
                print("fixture SQL cleanup failed; SQL output suppressed")
            else:
                print("temporary SQL fixture cleanup completed")
        except Exception:
            print("fixture SQL cleanup could not be verified; details suppressed")

        rows = bench.rows()
        output = Path(args.output)
        output.parent.mkdir(parents=True, exist_ok=True)
        doc = {
            "run_id": run_id,
            "target": {"spring": args.spring, "cpp": args.cpp},
            "started_at_utc": datetime.strptime(run_id[:12], "%y%m%d%H%M%S").replace(tzinfo=timezone.utc).isoformat(),
            "configured_iterations_per_endpoint": n,
            "http_endpoint_methods": len({row["endpoint"] for row in rows}),
            "http_total_samples": sum(int(row["samples"]) for row in rows),
            "elapsed_seconds": time.perf_counter() - bench.started,
            "measurement": "external client call-to-complete-response; persistent HTTP sessions; response bodies fully consumed; no server instrumentation",
            "request_data_bytes": "HTTP body plus URL query bytes; headers/TCP framing excluded",
            "websocket_report": "Separate report from scripts/benchmark_websocket_routes.py; it must run on agora-net because the token is bound to client IP",
            "notes": [
                "HTTP targets are local Docker host ports; HTTPS, Nginx, and public Internet latency are not included.",
                "C++ disconnect/delete controls use nonexistent positive IDs to avoid changing live sessions; these measure their safe no-op path.",
            ],
            "cleanup": {"remaining_canvases": len(canvas_ids), "sql_hard_delete_verified": cleanup_sql_ok,
                        "temporary_user_rows_remaining": 0 if cleanup_sql_ok else None},
            "results": rows,
        }
        output.write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
        csv_path = output.with_suffix(".csv")
        columns = list(rows[0].keys()) if rows else []
        with csv_path.open("w", newline="", encoding="utf-8") as fh:
            writer = csv.DictWriter(fh, fieldnames=columns)
            writer.writeheader()
            for row in rows:
                writer.writerow({**row, "statuses": json.dumps(row["statuses"], ensure_ascii=False)})
        print(f"report_json={output}")
        print(f"report_csv={csv_path}")
        print(f"endpoint_size_groups={len(rows)} elapsed={doc['elapsed_seconds']:.1f}s cleanup_sql_verified={cleanup_sql_ok}")

    return 0


if __name__ == "__main__":
    raise SystemExit(main())
