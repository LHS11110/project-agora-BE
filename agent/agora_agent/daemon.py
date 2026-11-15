import asyncio, json, os, signal, stat
from pathlib import Path
from dotenv import load_dotenv
from .engine import run_turn
from . import telemetry
from .settings import defaults, options
from .credentials import effective_key, personal_key
from openai import APIStatusError, APIConnectionError, APITimeoutError

MAX_FRAME = 1_000_000


def public_error(error):
    if isinstance(error, BaseExceptionGroup):
        return public_error(error.exceptions[0])
    if isinstance(error, ValueError) and str(error) == "AGENT_API_KEY_INVALID":
        return {"error":"AGENT_API_KEY_INVALID","message":"API 키 형식을 확인해주세요. 공백·줄바꿈 없이 입력해주세요."}
    if isinstance(error, ValueError) and str(error) == "AGENT_OPTIONS_INVALID":
        return {"error":"AGENT_OPTIONS_INVALID","message":"에이전트 설정값이 허용 범위를 벗어났습니다."}
    if isinstance(error, APIStatusError):
        if error.status_code == 429:
            return {
                "error": "AGENT_PROVIDER_BUSY",
                "message": "요청이 너무 많습니다. 잠시 후 다시 시도해주세요.",
            }
        if error.status_code in (401, 403):
            return {
                "error": "AGENT_PROVIDER_AUTH_FAILED",
                "message": "모델 제공 서비스 인증이 거부되었습니다. API 키와 모델 사용 권한을 확인해주세요.",
            }
        return {
            "error": "AGENT_PROVIDER_ERROR",
            "message": f"모델 제공 서비스가 오류를 반환했습니다. (HTTP {error.status_code}) 이미 반영된 변경은 유지됩니다.",
        }
    if isinstance(error, APITimeoutError):
        return {
            "error": "AGENT_PROVIDER_TIMEOUT",
            "message": "모델 제공 서비스 응답 시간이 초과되었습니다. 이미 반영된 변경은 유지됩니다.",
        }
    if isinstance(error, APIConnectionError):
        return {
            "error": "AGENT_PROVIDER_UNAVAILABLE",
            "message": "모델 제공 서비스에 연결할 수 없습니다. 이미 반영된 변경은 유지됩니다.",
        }
    return {
        "error": "AGENT_FAILED",
        "message": "에이전트 요청에 실패했습니다. 모델 설정·API 키·제공 서비스의 상태를 확인해주세요. 이미 반영된 변경은 유지됩니다.",
    }


async def serve(reader, writer):
    async def send(value):
        encoded = (json.dumps(value, ensure_ascii=False) + "\n").encode()
        if len(encoded) > MAX_FRAME:
            raise ValueError("Frame too large")
        writer.write(encoded)
        await writer.drain()

    async def bridge(name, arguments):
        nonlocal sequence
        sequence += 1
        await send(
            {"type": "tool", "id": sequence, "name": name, "arguments": arguments}
        )
        line = await reader.readline()
        if not line:
            raise ConnectionError("C++ bridge closed")
        value = json.loads(line)
        if value.get("type") != "tool_result" or value.get("id") != sequence:
            raise ValueError("Bridge mismatch")
        return value.get("result", {})

    async def progress(text):
        await send({"type": "output", "output": text})

    sequence = 0
    quota_context = None
    try:
        payload = json.loads(await asyncio.wait_for(reader.readline(), 10))
        selected_key = effective_key(payload)
        quota_context = telemetry.bind(selected_key)
        if payload.get("operation") == "status":
            await send(
                {
                    "telemetry": telemetry.snapshot(),
                    "defaults": options({}),
                    "ready": True,
                    "configured": bool(selected_key),
                    "credential_source": "personal" if personal_key(payload) else "server",
                    "default_configured": bool(os.getenv("AGENT_API_KEY")),
                    "model": os.getenv("AGENT_MODEL", "k-exaone_v2.5"),
                }
            )
            return
        if (
            not isinstance(payload.get("message"), str)
            or not 1 <= len(payload["message"]) <= 8000
        ):
            raise ValueError("Invalid message")
        if semaphore.locked():
            await send(
                {
                    "error": "AGENT_BUSY",
                    "message": "다른 에이전트 요청을 처리 중입니다. 잠시 후 다시 시도해주세요.",
                }
            )
            return
        async with semaphore:
            async with asyncio.timeout(
                min(220, int(os.getenv("AGENT_TIMEOUT_SECONDS", "210")))
            ):
                result = await run_turn(payload, bridge, progress)
                await send(result)
    except TimeoutError:
        try:
            await send(
                {
                    "error": "AGENT_TIMEOUT",
                    "message": "에이전트 제한 시간을 초과했습니다. 이미 반영된 변경은 유지됩니다.",
                }
            )
        except Exception:
            pass
    except Exception as error:
        # Provider exception strings can include request data; never return/log them.
        try:
            await send({**public_error(error), "telemetry": telemetry.snapshot()})
        except Exception:
            pass
    finally:
        if quota_context is not None:
            telemetry.unbind(quota_context)
        writer.close()
        try:
            await writer.wait_closed()
        except Exception:
            pass


async def main():
    global semaphore
    load_dotenv(Path(__file__).resolve().parents[1] / ".env")
    semaphore = asyncio.Semaphore(
        max(1, min(4, int(os.getenv("AGENT_MAX_CONCURRENT", "2"))))
    )
    path = Path(os.getenv("AGENT_SOCKET", "/run/agora-agent/agent.sock"))
    path.parent.mkdir(parents=True, exist_ok=True)
    if path.exists():
        if not stat.S_ISSOCK(path.lstat().st_mode):
            raise RuntimeError("Refusing to replace non-socket path")
        path.unlink()
    os.umask(0o007)
    server = await asyncio.start_unix_server(serve, str(path), limit=MAX_FRAME)
    os.chmod(path, 0o660)
    stop = asyncio.Event()
    for sig in (signal.SIGTERM, signal.SIGINT):
        asyncio.get_running_loop().add_signal_handler(sig, stop.set)
    async with server:
        await stop.wait()
    path.unlink(missing_ok=True)


if __name__ == "__main__":
    asyncio.run(main())
