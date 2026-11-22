import asyncio, json
from types import SimpleNamespace
from unittest.mock import AsyncMock, Mock
import pytest
from anthropic.types.beta import BetaMessage
from agora_agent import daemon, engine, anthropic_engine
from agora_agent.engine import run_turn
from test_agent import completion, tool


def claude_message(content, stop_reason, model="claude-opus-5-5"):
    return BetaMessage.model_validate({
        "id": "msg", "type": "message", "role": "assistant", "model": model,
        "content": content, "stop_reason": stop_reason, "stop_sequence": None,
        "usage": {"input_tokens": 1, "output_tokens": 1},
    })


class FakeStream:
    def __init__(self, message):
        self.message = message

    async def __aenter__(self):
        return self

    async def __aexit__(self, *args):
        return False

    async def get_final_message(self):
        if isinstance(self.message, Exception):
            raise self.message
        return self.message


def claude_client(messages):
    calls = []

    def stream(**params):
        # Snapshot: the loop appends to the same list after each call.
        calls.append({**params, "messages": list(params["messages"])})
        return FakeStream(messages.pop(0))

    client = SimpleNamespace(beta=SimpleNamespace(messages=SimpleNamespace(stream=stream)), close=AsyncMock())
    return client, calls


@pytest.mark.parametrize("config", [{"provider": "unknown"}, {"provider": 1}, {"model": "../etc"}, {"model": "a" * 129}, {"model": "gpt 5"}, {"base_url": "https://evil.example"}])
@pytest.mark.asyncio
async def test_provider_and_model_are_validated_before_any_request(config):
    client = SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=AsyncMock())))
    with pytest.raises(ValueError, match="AGENT_OPTIONS_INVALID"):
        await run_turn({"message": "test", "options": config}, AsyncMock(), AsyncMock(), client)
    client.chat.completions.create.assert_not_awaited()


@pytest.mark.asyncio
async def test_exaone_is_not_offered_and_its_server_key_is_never_used(monkeypatch):
    # EXAONE API terms forbid integrating it into products; a leftover key stays unused.
    monkeypatch.setenv("AGENT_API_KEY", "exaone-server-key")
    monkeypatch.delenv("AGENT_OPENAI_API_KEY", raising=False)
    with pytest.raises(ValueError, match="AGENT_OPTIONS_INVALID"):
        await run_turn({"message": "hi", "options": {"provider": "exaone"}}, AsyncMock(), AsyncMock())
    result = await run_turn({"message": "hi"}, AsyncMock(), AsyncMock())
    assert result["error"] == "AGENT_NOT_CONFIGURED" and "OpenAI" in result["message"]
    monkeypatch.delenv("AGENT_ANTHROPIC_API_KEY", raising=False)
    result = await run_turn({"message": "hi", "options": {"provider": "anthropic"}}, AsyncMock(), AsyncMock())
    assert result["error"] == "AGENT_NOT_CONFIGURED" and "Anthropic Claude" in result["message"]


@pytest.mark.parametrize("provider,base,model,token_field", [
    ("openai", "https://api.openai.com/v1", "gpt-5", "max_completion_tokens"),
    ("gemini", "https://generativelanguage.googleapis.com/v1beta/openai/", "gemini-2.5-flash", "max_tokens"),
    ("openrouter", "https://openrouter.ai/api/v1", "openrouter/auto", "max_tokens"),
])
@pytest.mark.asyncio
async def test_openai_compatible_providers_use_fixed_endpoints_and_accepted_params(monkeypatch, provider, base, model, token_field):
    create = AsyncMock(return_value=completion(answer="done"))
    client = SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)), close=AsyncMock())
    factory = Mock(return_value=client)
    monkeypatch.setattr(engine, "AsyncOpenAI", factory)
    result = await run_turn({"message": "test", "api_key": "personal-fixture", "options": {"provider": provider, "max_tokens": 4096, "reasoning_effort": "max"}}, AsyncMock(), AsyncMock())
    assert factory.call_args.kwargs["base_url"] == base
    assert factory.call_args.kwargs["api_key"] == "personal-fixture"
    sent = create.call_args.kwargs
    assert sent["model"] == model and sent[token_field] == 4096
    # EXAONE-only sampling and template extras are not sent elsewhere.
    assert "temperature" not in sent and "top_p" not in sent and "extra_body" not in sent
    assert sent.get("reasoning_effort") == ("high" if provider == "openai" else None)
    assert result["answer"] == "done" and result["provider"] == provider and result["model"] == model
    assert "personal-fixture" not in json.dumps(result)


@pytest.mark.asyncio
async def test_custom_model_name_is_passed_through(monkeypatch):
    create = AsyncMock(return_value=completion(answer="done"))
    client = SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)), close=AsyncMock())
    monkeypatch.setattr(engine, "AsyncOpenAI", Mock(return_value=client))
    await run_turn({"message": "t", "api_key": "k", "options": {"provider": "openai", "model": "gpt-4.1"}}, AsyncMock(), AsyncMock())
    assert create.call_args.kwargs["model"] == "gpt-4.1"
    assert "reasoning_effort" not in create.call_args.kwargs


@pytest.mark.asyncio
async def test_claude_runs_mcp_tools_and_returns_results_in_one_user_turn():
    client, calls = claude_client([
        claude_message([
            {"type": "thinking", "thinking": "", "signature": "sig"},
            {"type": "tool_use", "id": "tu1", "name": "get_objects", "input": {"ids": ["code-a"]}},
            {"type": "tool_use", "id": "tu2", "name": "get_objects", "input": {"ids": ["code-b"]}},
        ], "tool_use"),
        claude_message([{"type": "text", "text": "읽었습니다."}], "end_turn"),
    ])
    bridge = AsyncMock(return_value={"items": []})
    result = await run_turn({"message": "읽어줘", "options": {"provider": "anthropic", "reasoning_effort": "high"}, "history": [{"role": "assistant", "content": "이전 답"}]}, bridge, AsyncMock(), client)
    assert result["answer"] == "읽었습니다." and result["model"] == "claude-opus-5-5" and result["provider"] == "anthropic"
    assert bridge.await_count == 2
    first, second = calls
    assert first["model"] == "claude-opus-5-5" and first["output_config"] == {"effort": "high"}
    assert first["fallbacks"] == "default" and first["betas"] == ["server-side-fallback-2026-07-01"]
    assert "temperature" not in first and "top_p" not in first
    assert all(item["eager_input_streaming"] for item in first["tools"])
    # Leading assistant history is dropped: the conversation opens with the user.
    assert first["messages"][0]["role"] == "user"
    # Thinking block echoed back unchanged; both results in one user message.
    assert second["messages"][-2]["content"][0].type == "thinking"
    results = second["messages"][-1]
    assert results["role"] == "user" and [r["tool_use_id"] for r in results["content"]] == ["tu1", "tu2"]


@pytest.mark.asyncio
async def test_claude_truncated_tool_input_and_refusal_never_run_tools():
    for stop, expected in [("max_tokens", "토큰 상한"), ("refusal", "처리하지 않기로")]:
        client, _ = claude_client([claude_message([{"type": "tool_use", "id": "t", "name": "create_objects", "input": {"items": []}}], stop)])
        bridge = AsyncMock()
        result = await run_turn({"message": "x", "options": {"provider": "anthropic"}}, bridge, AsyncMock(), client)
        assert expected in result["answer"]
        bridge.assert_not_awaited()


@pytest.mark.asyncio
async def test_claude_unparseable_tool_json_reissues_turn_with_a_bound():
    client, calls = claude_client([ValueError("bad json"), claude_message([{"type": "text", "text": "ok"}], "end_turn")])
    result = await run_turn({"message": "x", "options": {"provider": "anthropic"}}, AsyncMock(), AsyncMock(), client)
    assert result["answer"] == "ok" and len(calls) == 2
    client, _ = claude_client([ValueError("bad")] * 3)
    # The MCP task group wraps the error; the daemon unwraps groups for the user message.
    with pytest.raises(BaseExceptionGroup) as raised:
        await run_turn({"message": "x", "options": {"provider": "anthropic", "max_steps": 5}}, AsyncMock(), AsyncMock(), client)
    assert raised.group_contains(ValueError)
    assert daemon.public_error(raised.value)["error"] == "AGENT_FAILED"


def test_claude_params_skip_effort_and_fallbacks_for_models_without_them():
    params = anthropic_engine.request_params("claude-haiku-4-5", {"max_tokens": 1024, "reasoning_effort": "low"}, "s", [], [])
    assert "output_config" not in params and "fallbacks" not in params and "betas" not in params
    params = anthropic_engine.request_params("claude-haiku-5-5", {"max_tokens": 1024, "reasoning_effort": "max"}, "s", [], [])
    assert params["output_config"] == {"effort": "max"} and "fallbacks" not in params


@pytest.mark.asyncio
async def test_status_lists_providers_without_keys(tmp_path, monkeypatch):
    daemon.semaphore = asyncio.Semaphore(1)
    monkeypatch.setenv("AGENT_SOCKET", str(tmp_path / "agent.sock"))
    monkeypatch.setenv("AGENT_API_KEY", "legacy-exaone-secret")
    monkeypatch.setenv("AGENT_OPENAI_API_KEY", "openai-server-secret")
    path = str(tmp_path / "agent.sock")
    async with await asyncio.start_unix_server(daemon.serve, path, limit=daemon.MAX_FRAME):
        reader, writer = await asyncio.open_unix_connection(path)
        writer.write(b'{"operation":"status","options":{"provider":"openai","model":"gpt-4.1"}}\n')
        await writer.drain()
        value = json.loads(await reader.readline())
        writer.close()
        await writer.wait_closed()
    assert value["provider"] == "openai" and value["model"] == "gpt-4.1"
    assert value["configured"] is True and value["default_configured"] is True
    listed = {entry["id"]: entry for entry in value["providers"]}
    assert listed["anthropic"]["default_configured"] is False and listed["anthropic"]["default_model"] == "claude-opus-5-5"
    assert "exaone" not in listed
    assert "secret" not in json.dumps(value)


def test_anthropic_errors_map_to_public_messages():
    import anthropic, httpx
    request = httpx.Request("POST", "https://api.anthropic.com/v1/messages")
    error = anthropic.AuthenticationError("denied", response=httpx.Response(401, request=request), body=None)
    assert daemon.public_error(error)["error"] == "AGENT_PROVIDER_AUTH_FAILED"
    assert daemon.public_error(anthropic.APITimeoutError(request=request))["error"] == "AGENT_PROVIDER_TIMEOUT"
