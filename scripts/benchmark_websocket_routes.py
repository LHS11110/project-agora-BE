#!/usr/bin/env python3
"""External WebSocket handshake benchmark from a peer on the shared Docker network.

Using one client container for both Spring access-token issuance and C++ WS avoids
host-port NAT giving the token and WebSocket different client IPs.
"""
from __future__ import annotations

import argparse
import base64
import http.client
import json
import math
import os
import secrets
import socket
import ssl
import statistics
import time
from collections import Counter
from datetime import datetime, timezone
from pathlib import Path
from urllib.parse import quote


def envfile(path: str) -> dict[str, str]:
    out = {}
    for line in Path(path).read_text(encoding="utf-8").splitlines():
        line = line.strip()
        if line and not line.startswith("#") and "=" in line:
            k, v = line.split("=", 1)
            out[k.strip()] = v.strip().strip('"').strip("'")
    return out


def api_json(host: str, method: str, path: str, body: object | None = None,
             token: str | None = None, timeout: float = 15, port: int = 8080,
             internal_token: str | None = None) -> tuple[int, bytes, dict[str, str]]:
    payload = None if body is None else json.dumps(body, separators=(",", ":")).encode()
    headers = {}
    if payload is not None:
        headers["Content-Type"] = "application/json"
    if token:
        headers["Authorization"] = "Bearer " + token
    if internal_token:
        headers["X-Agora-Internal-Token"] = internal_token
    conn = http.client.HTTPSConnection(host, port, timeout=timeout, context=ssl.create_default_context(cafile=os.environ.get("SERVICE_TLS_CA")))
    try:
        conn.request(method, path, body=payload, headers=headers)
        response = conn.getresponse()
        data = response.read()
        return response.status, data, dict(response.getheaders())
    finally:
        conn.close()


def read_exact(sock: socket.socket, n: int, pending: bytearray) -> bytes:
    while len(pending) < n:
        chunk = sock.recv(max(4096, n - len(pending)))
        if not chunk:
            raise OSError("socket closed")
        pending.extend(chunk)
    result = bytes(pending[:n])
    del pending[:n]
    return result


def read_frame(sock: socket.socket, pending: bytes) -> tuple[bytes, int]:
    buf = bytearray(pending)
    total = 0
    while True:
        first, second = read_exact(sock, 2, buf)
        total += 2
        opcode = first & 0x0F
        length = second & 0x7F
        if length == 126:
            length = int.from_bytes(read_exact(sock, 2, buf), "big")
            total += 2
        elif length == 127:
            length = int.from_bytes(read_exact(sock, 8, buf), "big")
            total += 8
        mask = read_exact(sock, 4, buf) if second & 0x80 else b""
        total += len(mask)
        payload = read_exact(sock, length, buf)
        total += length
        if mask:
            payload = bytes(b ^ mask[i % 4] for i, b in enumerate(payload))
        if opcode == 0x9:
            key = secrets.token_bytes(4)
            masked = bytes(b ^ key[i % 4] for i, b in enumerate(payload))
            sock.sendall(bytes((0x8A, 0x80 | len(payload))) + key + masked)
            continue
        if opcode == 0x8:
            raise OSError("peer closed before first data frame")
        return payload, total


def ws_sample(host: str, route: str, token: str, expected_type: str) -> dict[str, object]:
    target = route + "?token=" + quote(token, safe="._-")
    key = base64.b64encode(secrets.token_bytes(16)).decode("ascii")
    request = (
        f"GET {target} HTTP/1.1\r\nHost: {host}:8002\r\nUpgrade: websocket\r\n"
        f"Connection: Upgrade\r\nSec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode("ascii")
    started = time.perf_counter_ns()
    status: int | str = "ERR"
    response_bytes = 0
    first_type = ""
    error_class = ""
    response_meta: dict[str, str] = {}
    sock = None
    try:
        sock = ssl.create_default_context(cafile=os.environ.get("SERVICE_TLS_CA")).wrap_socket(socket.create_connection((host, 8002), timeout=10), server_hostname=host)
        sock.settimeout(10)
        sock.sendall(request)
        data = bytearray()
        while b"\r\n\r\n" not in data:
            part = sock.recv(4096)
            if not part:
                break
            data.extend(part)
            if len(data) > 32768:
                break
        raw = bytes(data)
        boundary = raw.find(b"\r\n\r\n")
        if boundary >= 0:
            header = raw[:boundary + 4]
            pending = raw[boundary + 4:]
            line = header.split(b"\r\n", 1)[0].split()
            status = int(line[1]) if len(line) > 1 and line[1].isdigit() else "ERR"
            response_bytes = len(header)
            if status == 101:
                frame, frame_len = read_frame(sock, pending)
                response_bytes += frame_len
                try:
                    first_type = json.loads(frame.decode("utf-8")).get("type", "")
                except (UnicodeDecodeError, ValueError, AttributeError):
                    first_type = "invalid_json"
                mask = secrets.token_bytes(4)
                close_payload = b"\x03\xe8"
                masked = bytes(b ^ mask[i % 4] for i, b in enumerate(close_payload))
                sock.sendall(bytes((0x88, 0x80 | len(close_payload))) + mask + masked)
            else:
                headers = {}
                for line in header.decode("latin1", "replace").split("\r\n")[1:]:
                    if ":" in line:
                        k, v = line.split(":", 1)
                        headers[k.strip().lower()] = v.strip()
                body_len = min(int(headers.get("content-length", "0") or "0"), 4096)
                response_meta = {k: headers[k] for k in ("content-length", "transfer-encoding", "upgrade", "connection", "server") if k in headers}
                if body_len:
                    body = bytearray(pending)
                    if len(body) < body_len:
                        while len(body) < body_len:
                            part = sock.recv(body_len - len(body))
                            if not part:
                                break
                            body.extend(part)
                    message = bytes(body[:body_len]).lower()
                    if b"unauthorized" in message or b"invalid" in message:
                        error_class = "authentication_rejected"
                    elif b"canvas_id" in message:
                        error_class = "canvas_id_rejected"
                    else:
                        error_class = "http_rejected"
                else:
                    error_class = "empty_http_response"
    except (OSError, ValueError, IndexError):
        pass
    finally:
        if sock is not None:
            try:
                sock.close()
            except OSError:
                pass
    elapsed = (time.perf_counter_ns() - started) / 1_000_000
    return {
        "latency_ms": elapsed, "request_bytes": len(target.encode("ascii")),
        "response_bytes": response_bytes, "http_status": status,
        "first_frame_type": first_type, "error_class": error_class, "response_meta": response_meta,
        "success": status == 101 and first_type == expected_type,
    }


def stats(entries: list[dict[str, object]]) -> dict[str, object]:
    times = sorted(float(x["latency_ms"]) for x in entries)
    n = len(times)
    p95 = times[max(0, math.ceil(.95 * n) - 1)]
    return {
        "samples": n,
        "successes": sum(bool(x["success"]) for x in entries),
        "statuses": dict(Counter(str(x["http_status"]) for x in entries)),
        "first_frame_types": dict(Counter(str(x["first_frame_type"]) for x in entries)),
        "error_classes": dict(Counter(str(x["error_class"]) for x in entries if x["error_class"])),
        "response_meta": entries[0].get("response_meta", {}) if entries else {},
        "mean_ms": statistics.fmean(times),
        "p50_ms": times[(n - 1) // 2],
        "p95_ms": p95,
        "min_ms": times[0], "max_ms": times[-1],
        "avg_request_bytes": statistics.fmean(int(x["request_bytes"]) for x in entries),
        "avg_response_bytes": statistics.fmean(int(x["response_bytes"]) for x in entries),
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--env", required=True)
    parser.add_argument("--iterations", type=int, default=1000)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    env = envfile(args.env)
    run_id = datetime.now(timezone.utc).strftime("%y%m%d%H%M%S") + secrets.token_hex(3)
    admin_email = "admin@agora.com"
    status, raw, _ = api_json("agora-spring", "POST", "/api/auth/login",
                              {"email": admin_email, "password": env["ADMIN_PASSWORD"]})
    if status != 200:
        raise RuntimeError(f"admin login failed with HTTP {status}")
    admin_token = json.loads(raw).get("accessToken", "")
    if not admin_token:
        raise RuntimeError("admin login did not provide a token")
    status, raw, _ = api_json("agora-spring", "POST", "/api/canvases",
                              {"canvasName": f"wsbench-{run_id}", "description": ""}, admin_token)
    if status != 201:
        raise RuntimeError(f"temporary canvas creation failed with HTTP {status}")
    canvas_id = int(json.loads(raw)["canvas_id"])
    access_token = ""
    results: dict[str, object] = {}
    remaining_canvas = True
    start = time.perf_counter()
    try:
        status, raw, _ = api_json("agora-spring", "POST", f"/api/canvases/{canvas_id}/access",
                                  {}, admin_token)
        if status != 200:
            raise RuntimeError(f"temporary canvas access failed with HTTP {status}")
        access_token = json.loads(raw).get("canvas_access_token", "")
        if not access_token:
            raise RuntimeError("access API did not return a canvas token")
        for name, route, expected_type in (
            ("C++ WS canvas", f"/ws/canvas/{canvas_id}", "init_items"),
            ("C++ WS RTC", f"/ws/rtc/canvas/{canvas_id}", "rtc_ready"),
        ):
            entries: list[dict[str, object]] = []
            failures = 0
            for _ in range(args.iterations):
                sample = ws_sample("agora-cpp", route, access_token, expected_type)
                entries.append(sample)
                if sample["success"]:
                    failures = 0
                else:
                    failures += 1
                    if failures >= 3:
                        break
            results[name] = {"expected_first_frame": expected_type, **stats(entries)}
            print(f"{name}: n={len(entries)} success={results[name]['successes']} status={results[name]['statuses']}", flush=True)

        # Let the asynchronous Canvas WebSocket close path clear its SQL session.
        deadline = time.monotonic() + 30
        while time.monotonic() < deadline:
            status, raw, _ = api_json("agora-spring", "GET", "/api/test/cpp-active-canvases", token=admin_token)
            try:
                active = json.loads(raw).get("canvases", [])
                if status == 200 and all(int(x.get("canvas_id", -1)) != canvas_id for x in active):
                    break
            except (ValueError, TypeError):
                pass
            time.sleep(.25)
        for _ in range(40):
            status, raw, _ = api_json("agora-cpp", "DELETE", f"/api/canvas/{canvas_id}",
                                      port=8000, internal_token=env["CPP_INTERNAL_API_TOKEN"])
            if status == 200 and json.loads(raw).get("removed") is True:
                break
            time.sleep(.25)
        for _ in range(20):
            status, _, _ = api_json("agora-spring", "DELETE", f"/api/canvases/{canvas_id}", token=admin_token)
            if status == 204:
                remaining_canvas = False
                break
            time.sleep(.25)
    finally:
        if remaining_canvas:
            try:
                api_json("agora-cpp", "DELETE", f"/api/canvas/{canvas_id}",
                         port=8000, internal_token=env["CPP_INTERNAL_API_TOKEN"])
                api_json("agora-spring", "DELETE", f"/api/canvases/{canvas_id}", token=admin_token)
            except Exception:
                pass
        doc = {
            "run_id": run_id,
            "target": "temporary client on Docker agora-net; Spring and C++ receive the same client IP",
            "started_at_utc": datetime.now(timezone.utc).isoformat(),
            "configured_iterations_per_route": args.iterations,
            "elapsed_seconds": time.perf_counter() - start,
            "measurement": "TCP connect through HTTP 101 and first complete WebSocket server frame; no server instrumentation",
            "cleanup": {"temporary_canvas_removed": not remaining_canvas},
            "results": results,
        }
        Path(args.output).write_text(json.dumps(doc, ensure_ascii=False, indent=2), encoding="utf-8")
        print(f"report={args.output} cleanup_canvas_removed={not remaining_canvas}", flush=True)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
