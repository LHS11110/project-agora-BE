from datetime import UTC, datetime
import asyncio, json, os, re, signal, sqlite3, stat
from pathlib import Path
from dotenv import load_dotenv
from .engine import run_turn
from . import telemetry
from .settings import options
from .providers import canvas_external_models, model_for, server_key, spec
import anthropic
from openai import APIStatusError, APIConnectionError, APITimeoutError

# Both SDKs raise their own classes; the user-facing mapping is the same.
STATUS_ERRORS = (APIStatusError, anthropic.APIStatusError)
TIMEOUT_ERRORS = (APITimeoutError, anthropic.APITimeoutError)
CONNECTION_ERRORS = (APIConnectionError, anthropic.APIConnectionError)

MAX_FRAME = 1_000_000
EXTERNAL_DAILY_STEPS = 80
EXTERNAL_MAX_STEPS = 10


def reserve_external_steps(user_id, steps):
    # Persist only account IDs, dates and reserved work. Credentials never enter this file.
    path = Path(os.getenv("AGENT_SOCKET", "/run/agora-agent/agent.sock")).with_name("external-quota.sqlite3")
    descriptor = os.open(path, os.O_CREAT | os.O_RDWR, 0o600)
    os.close(descriptor)
    day = datetime.now(UTC).date().isoformat()
    connection = sqlite3.connect(path, timeout=2)
    try:
        with connection:
            connection.execute("BEGIN IMMEDIATE")
            connection.execute("CREATE TABLE IF NOT EXISTS budget (user_id INTEGER PRIMARY KEY, day TEXT, steps INTEGER)")
            connection.execute("DELETE FROM budget WHERE day < ?", (day,))
            result = connection.execute(
                """INSERT INTO budget VALUES (?, ?, ?)
                   ON CONFLICT(user_id) DO UPDATE SET steps = steps + excluded.steps
                   WHERE steps + excluded.steps <= ?""",
                (user_id, day, steps, EXTERNAL_DAILY_STEPS),
            )
            return result.rowcount == 1
    finally:
        connection.close()


CANVAS_PROVIDER = "anthropic"
CANVAS_MODELS = frozenset({"", "claude-opus-5-5", "claude-sonnet-5-5", "claude-haiku-5-5"})
CANVAS_CONSENT_VERSION = "agent-adult-v3"


def public_error(error):
    if isinstance(error, BaseExceptionGroup):
        return public_error(error.exceptions[0])
    if isinstance(error, ValueError) and str(error) == "AGENT_API_KEY_INVALID":
        return {"error":"AGENT_API_KEY_INVALID","message":"이 연결에 허용된 개발자 API 키를 확인해주세요."}
    if isinstance(error, ValueError) and str(error) == "AGENT_OPTIONS_INVALID":
        return {"error":"AGENT_OPTIONS_INVALID","message":"에이전트 설정값이 허용 범위를 벗어났습니다."}
    if isinstance(error, ValueError) and str(error) == "AGENT_CONSENT_REQUIRED":
        return {"error":"AGENT_CONSENT_REQUIRED","message":"에이전트의 데이터 전송 조건에 동의한 뒤 다시 요청해주세요."}
    if isinstance(error, STATUS_ERRORS):
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
    if isinstance(error, TIMEOUT_ERRORS):
        return {
            "error": "AGENT_PROVIDER_TIMEOUT",
            "message": "모델 제공 서비스 응답 시간이 초과되었습니다. 이미 반영된 변경은 유지됩니다.",
        }
    if isinstance(error, CONNECTION_ERRORS):
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
        if name != "create_objects":
            return {"error": "AGENT_TOOL_DENIED"}
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
        supplied_options = payload.get("options", {})
        if not isinstance(supplied_options, dict):
            raise ValueError("AGENT_OPTIONS_INVALID")
        config = options({"provider": CANVAS_PROVIDER, **supplied_options})
        external = config["provider"] == "openrouter"
        key = payload.get("api_key", "")
        if external:
            if config["model"] not in canvas_external_models():
                raise ValueError("AGENT_OPTIONS_INVALID")
            config["max_steps"] = min(config["max_steps"], EXTERNAL_MAX_STEPS)
            config["max_tokens"] = min(config["max_tokens"], 8192)
            if key and (payload.get("operation") != "run" or not isinstance(key, str)
                        or len(key) > 256 or not re.fullmatch(r"sk-or-v1-[a-zA-Z0-9_-]+", key)):
                raise ValueError("AGENT_API_KEY_INVALID")
        elif config["provider"] != CANVAS_PROVIDER or config["model"] not in CANVAS_MODELS or key:
            raise ValueError("AGENT_OPTIONS_INVALID" if not key else "AGENT_API_KEY_INVALID")
        provider = spec(config["provider"])
        selected_key = "" if external else server_key(provider)
        if not external:
            quota_context = telemetry.bind(selected_key)
        if payload.get("operation") == "status":
            await send(
                {
                    "telemetry": telemetry.snapshot(),
                    "defaults": options({"provider": CANVAS_PROVIDER}),
                    "ready": True,
                    "configured": external or bool(selected_key),
                    "credential_source": "request" if external else "server",
                    "provider": provider["id"],
                    "default_configured": bool(server_key(provider)),
                    "model": model_for(provider, config["model"]),
                }
            )
            return
        if payload.get("operation") != "run":
            raise ValueError("AGENT_OPTIONS_INVALID")
        if payload.get("consent_version") != CANVAS_CONSENT_VERSION:
            raise ValueError("AGENT_CONSENT_REQUIRED")
        if (
            not isinstance(payload.get("message"), str)
            or not 1 <= len(payload["message"]) <= 8000
        ):
            raise ValueError("Invalid message")
        anchor = payload.get("anchor")
        if not isinstance(anchor, dict):
            anchor = {}
        anchor = {
            axis: value
            for axis in ("x", "y")
            if type(value := anchor.get(axis)) in (int, float) and -1e9 <= value <= 1e9
        }
        if semaphore.locked():
            await send(
                {
                    "error": "AGENT_BUSY",
                    "message": "다른 에이전트 요청을 처리 중입니다. 잠시 후 다시 시도해주세요.",
                }
            )
            return
        if external:
            user_id = payload.get("user_id")
            if not key or type(user_id) is not int or user_id <= 0:
                raise ValueError("AGENT_API_KEY_INVALID")
            if not reserve_external_steps(user_id, config["max_steps"]):
                await send({"error": "AGENT_DAILY_LIMIT", "message": "외부 모델 일일 작업 한도에 도달했습니다."})
                return
        async with semaphore:
            async with asyncio.timeout(
                min(220, int(os.getenv("AGENT_TIMEOUT_SECONDS", "210")))
            ):
                result = await run_turn({
                    "message": payload["message"],
                    "anchor": anchor,
                    "options": config,
                    **({"api_key": key} if external else {}),
                }, bridge, progress, create_only=True, **({"external_canvas": True} if external else {}))
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
