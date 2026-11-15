import asyncio, json
from types import SimpleNamespace
from unittest.mock import AsyncMock
import pytest
from openai.types.chat import ChatCompletion
from agora_agent.engine import run_turn
from agora_agent import daemon


def completion(calls=None, answer=None):
    return ChatCompletion.model_validate(
        {
            "id": "test",
            "created": 1,
            "model": "mock",
            "object": "chat.completion",
            "choices": [
                {
                    "index": 0,
                    "finish_reason": "tool_calls" if calls else "stop",
                    "message": {
                        "role": "assistant",
                        "content": answer,
                        "tool_calls": calls,
                    },
                }
            ],
        }
    )


def tool(name, args):
    return {
        "id": name,
        "type": "function",
        "function": {"name": name, "arguments": json.dumps(args)},
    }


@pytest.mark.asyncio
async def test_mcp_read_modify_create_and_deleted_tool_disabled():
    responses = [
        completion([tool("get_objects", {"ids": ["code-a"]})]),
        completion(
            [
                tool(
                    "patch_objects",
                    {
                        "changes": [
                            {
                                "id": "code-a",
                                "revision": 4,
                                "patch": {"text": "a=1\nb=2"},
                            }
                        ]
                    },
                ),
                tool(
                    "create_objects",
                    {
                        "items": [
                            {"kind": "text", "renderMode": "code", "text": "print(a+b)"}
                        ]
                    },
                ),
            ]
        ),
        completion(answer="수정하고 생성했습니다."),
    ]
    create = AsyncMock(side_effect=responses)
    client = SimpleNamespace(
        chat=SimpleNamespace(completions=SimpleNamespace(create=create))
    )
    calls = []

    async def bridge(name, args):
        calls.append((name, args))
        if name == "get_objects":
            return {
                "objects": [{"id": "code-a", "revision": 4, "item": {"text": "a=1"}}]
            }
        return {"object_ids": ["new-code"]}

    progress = AsyncMock()
    result = await asyncio.wait_for(
        run_turn(
            {
                "canvas_id": 1,
                "message": "기존 코드에 더해줘",
                "selected_ids": ["code-a"],
            },
            bridge,
            progress,
            client,
        ),
        5,
    )
    assert result["answer"] == "수정하고 생성했습니다."
    assert [c[0] for c in calls] == ["get_objects", "patch_objects", "create_objects"]
    assert calls[1][1]["changes"][0]["revision"] == 4
    tools = create.call_args_list[0].kwargs["tools"]
    assert "delete_objects" not in [t["function"]["name"] for t in tools]
    assert create.call_args.kwargs["model"] == "k-exaone_v2.5"
    assert progress.await_count >= 5


@pytest.mark.asyncio
async def test_conflict_returned_to_model_and_delete_opt_in():
    create = AsyncMock(
        side_effect=[
            completion(
                [tool("delete_objects", {"objects": [{"id": "a", "revision": 1}]})]
            ),
            completion(answer="충돌하여 삭제하지 않았습니다."),
        ]
    )
    client = SimpleNamespace(
        chat=SimpleNamespace(completions=SimpleNamespace(create=create))
    )
    bridge = AsyncMock(return_value={"error": "CANVAS_WRITE_CONFLICT"})
    result = await run_turn(
        {"message": "지워줘", "allow_delete": True}, bridge, AsyncMock(), client
    )
    assert "충돌" in result["answer"]
    assert "delete_objects" in [
        t["function"]["name"] for t in create.call_args.kwargs["tools"]
    ]
    assert any(
        "CANVAS_WRITE_CONFLICT" in m["content"]
        for m in create.call_args.kwargs["messages"]
        if m["role"] == "tool"
    )


@pytest.mark.asyncio
async def test_unconfigured(monkeypatch):
    monkeypatch.delenv("AGENT_API_KEY", raising=False)
    assert (await run_turn({"message": "hi"}, AsyncMock(), AsyncMock()))[
        "error"
    ] == "AGENT_NOT_CONFIGURED"


@pytest.mark.asyncio
async def test_unix_duplex_bridge_and_status(tmp_path, monkeypatch):
    daemon.semaphore = asyncio.Semaphore(1)

    async def fake_turn(payload, bridge, progress):
        await progress("읽는 중")
        result = await bridge("get_objects", {"ids": ["a"]})
        return {"answer": result["text"]}

    monkeypatch.setattr(daemon, "run_turn", fake_turn)
    path = str(tmp_path / "agent.sock")
    async with await asyncio.start_unix_server(
        daemon.serve, path, limit=daemon.MAX_FRAME
    ):
        reader, writer = await asyncio.open_unix_connection(path)
        writer.write(b'{"message":"hello"}\n')
        await writer.drain()
        assert json.loads(await reader.readline())["type"] == "output"
        tool_request = json.loads(await reader.readline())
        assert tool_request["name"] == "get_objects"
        writer.write(
            (
                json.dumps(
                    {
                        "type": "tool_result",
                        "id": tool_request["id"],
                        "result": {"text": "done"},
                    }
                )
                + "\n"
            ).encode()
        )
        await writer.drain()
        assert json.loads(await reader.readline()) == {"answer": "done"}
        writer.close()
        await writer.wait_closed()
        reader, writer = await asyncio.open_unix_connection(path)
        writer.write(b'{"operation":"status"}\n')
        await writer.drain()
        assert json.loads(await reader.readline())["ready"] is True
        writer.close()
        await writer.wait_closed()


@pytest.mark.asyncio
async def test_large_tool_result_is_rejected_without_partial_source():
    create = AsyncMock(
        side_effect=[
            completion([tool("get_objects", {"ids": ["a"]})]),
            completion(answer="객체를 나누어 요청해주세요."),
        ]
    )
    client = SimpleNamespace(
        chat=SimpleNamespace(completions=SimpleNamespace(create=create))
    )
    await run_turn(
        {"message": "읽어줘"},
        AsyncMock(return_value={"text": "secret-source-" * 20000}),
        AsyncMock(),
        client,
    )
    messages = create.call_args.kwargs["messages"]
    result = json.loads(next(m["content"] for m in messages if m["role"] == "tool"))
    assert result["error"] == "TOOL_RESULT_TOO_LARGE"
    assert "secret-source-" not in json.dumps(result)


@pytest.mark.asyncio
async def test_provider_busy_returns_immediately_without_replaying_committed_tool(monkeypatch):
    import httpx
    from openai import RateLimitError
    from agora_agent import engine

    busy = RateLimitError(
        "do not disclose this",
        response=httpx.Response(
            429, request=httpx.Request("POST", "https://example.test")
        ),
        body={"error": "busy"},
    )
    create = AsyncMock(
        side_effect=[
            completion(
                [tool("create_objects", {"items": [{"kind": "text", "text": "hi"}]})]
            ),
            busy,
            completion(answer="완료"),
        ]
    )
    client = SimpleNamespace(
        chat=SimpleNamespace(completions=SimpleNamespace(create=create))
    )
    bridge = AsyncMock(return_value={"object_ids": ["a"]})
    monkeypatch.setattr(engine.asyncio, "sleep", AsyncMock())
    with pytest.raises(ExceptionGroup):
        await run_turn({"message": "생성해줘"}, bridge, AsyncMock(), client)
    assert bridge.await_count == 1 and create.await_count == 2
    safe = daemon.public_error(ExceptionGroup("wrapped", [busy]))
    assert safe[
        "error"
    ] == "AGENT_PROVIDER_BUSY" and "do not disclose" not in json.dumps(safe)

    assert safe["message"] == "요청이 너무 많습니다. 잠시 후 다시 시도해주세요."

@pytest.mark.asyncio
@pytest.mark.parametrize("async_parse",[True,False])
async def test_raw_response_headers_and_custom_options(monkeypatch, tmp_path, async_parse):
    from agora_agent import telemetry
    monkeypatch.setenv('AGENT_SOCKET', str(tmp_path/'agent.sock'))
    monkeypatch.setattr(telemetry, '_state', None)
    raw=SimpleNamespace(headers={'x-second-rate-limit':'1','x-second-rate-limit-remaining':'0','x-minute-rate-limit':'60','x-minute-rate-limit-remaining':'59','x-day-rate-limit':'100','x-day-rate-limit-remaining':'91','authorization':'hidden'},status_code=200,parse=AsyncMock(return_value=completion(answer='done')))
    if not async_parse:raw.parse=lambda: completion(answer="done")
    create=AsyncMock(return_value=raw)
    client=SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(with_raw_response=SimpleNamespace(create=create))))
    config={'reasoning_effort':'max','max_tokens':16384,'max_steps':3,'temperature':.7,'top_p':.8}
    result=await run_turn({'message':'test','options':config},AsyncMock(),AsyncMock(),client)
    assert result['telemetry']['limits']['day']=={'limit':100,'remaining':91}
    assert create.call_args.kwargs['extra_body']['reasoning_effort']=='max'
    assert create.call_args.kwargs['max_tokens']==16384
    assert create.call_args.kwargs['temperature']==.7
    assert result['options']==config
    saved=(tmp_path/'quota.json').read_text()
    assert 'hidden' not in saved and 'authorization' not in saved
    monkeypatch.setattr(telemetry, '_state', None)
    assert telemetry.snapshot()['limits']['minute']['remaining']==59


def test_quota_errors_retry_after_and_key_rotation(monkeypatch,tmp_path):
    from agora_agent import telemetry
    monkeypatch.setenv('AGENT_SOCKET',str(tmp_path/'agent.sock'))
    monkeypatch.setenv('AGENT_API_KEY','test-key-one')
    monkeypatch.setattr(telemetry,'_state',None)
    data=telemetry.record({'x-day-rate-limit':'100','x-day-rate-limit-remaining':'0','retry-after-ms':'1250','x-second-rate-limit':'NaN'},429)
    assert data['status']==429 and data['retry_at']-data['observed_at']==1.25
    assert 'second' not in data['limits']
    monkeypatch.setenv('AGENT_API_KEY','test-key-two')
    monkeypatch.setattr(telemetry,'_state',None)
    assert telemetry.snapshot()=={}


@pytest.mark.parametrize('config',[{'reasoning_effort':'medium'},{'max_tokens':1000000},{'max_steps':0},{'max_steps':1.5},{'temperature':float('nan')},{'top_p':False},{'api_key':'forged'}])
@pytest.mark.asyncio
async def test_invalid_options_never_call_model(config):
    client=SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=AsyncMock())))
    with pytest.raises(ValueError,match='AGENT_OPTIONS_INVALID'):
        await run_turn({'message':'test','options':config},AsyncMock(),AsyncMock(),client)
    client.chat.completions.create.assert_not_awaited()

@pytest.mark.asyncio
async def test_truncated_reasoning_reports_token_budget_without_running_partial_tool():
    response=completion(answer=None)
    response.choices[0].finish_reason='length'
    create=AsyncMock(return_value=response)
    bridge=AsyncMock()
    result=await run_turn({'message':'test','options':{'max_tokens':512}},bridge,AsyncMock(),SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create))))
    assert result['limited'] is True and '토큰 상한' in result['answer']
    bridge.assert_not_awaited()

@pytest.mark.asyncio
async def test_personal_key_overrides_server_without_entering_prompt_or_result(monkeypatch):
    from unittest.mock import Mock
    from agora_agent import engine
    monkeypatch.setenv('AGENT_API_KEY','server-key-fixture')
    create=AsyncMock(return_value=completion(answer='done'))
    client=SimpleNamespace(chat=SimpleNamespace(completions=SimpleNamespace(create=create)),close=AsyncMock())
    factory=Mock(return_value=client)
    monkeypatch.setattr(engine,'AsyncOpenAI',factory)
    result=await engine.run_turn({'message':'test','api_key':'personal-key-fixture'},AsyncMock(),AsyncMock())
    assert factory.call_args.kwargs['api_key']=='personal-key-fixture'
    assert 'personal-key-fixture' not in json.dumps(create.call_args.kwargs)
    assert 'personal-key-fixture' not in json.dumps(result)
    assert 'server-key-fixture' not in json.dumps(result)
    client.close.assert_awaited_once()


@pytest.mark.asyncio
async def test_concurrent_personal_keys_keep_quota_isolated_without_disk_storage(monkeypatch,tmp_path):
    from agora_agent import telemetry
    monkeypatch.setenv('AGENT_SOCKET',str(tmp_path/'agent.sock'))
    monkeypatch.setenv('AGENT_API_KEY','server-key-fixture')
    monkeypatch.setattr(telemetry,'_state',None)
    telemetry.record({'x-day-rate-limit':'100','x-day-rate-limit-remaining':'90'},200)
    async def personal(key,remaining):
        token=telemetry.bind(key)
        try:
            telemetry.record({'x-day-rate-limit':'100','x-day-rate-limit-remaining':str(remaining)},200)
            await asyncio.sleep(0)
            return telemetry.snapshot()['limits']['day']['remaining']
        finally:telemetry.unbind(token)
    assert await asyncio.gather(personal('personal-one',11),personal('personal-two',22))==[11,22]
    assert telemetry.snapshot()['limits']['day']['remaining']==90
    disk=(tmp_path/'quota.json').read_text()
    assert 'personal-one' not in disk and 'personal-two' not in disk
    assert '11.0' not in disk and '22.0' not in disk


@pytest.mark.parametrize('key',[True,42,'a\nb','a b','한글','a'*4097])
def test_personal_key_invalid_shapes_rejected(key):
    from agora_agent.credentials import personal_key
    with pytest.raises(ValueError,match='AGENT_API_KEY_INVALID'):
        personal_key({'api_key':key})


@pytest.mark.asyncio
async def test_status_personal_credentials_does_not_return_or_persist_key(tmp_path,monkeypatch):
    from agora_agent import telemetry
    daemon.semaphore=asyncio.Semaphore(1)
    monkeypatch.delenv('AGENT_API_KEY',raising=False)
    monkeypatch.setenv('AGENT_SOCKET',str(tmp_path/'agent.sock'))
    path=str(tmp_path/'agent.sock')
    async with await asyncio.start_unix_server(daemon.serve,path,limit=daemon.MAX_FRAME):
        reader,writer=await asyncio.open_unix_connection(path)
        writer.write(b'{"operation":"status","api_key":"personal-fixture-no-secret"}\n');await writer.drain()
        value=json.loads(await reader.readline())
        assert value['configured'] is True and value['default_configured'] is False
        assert value['credential_source']=='personal'
        assert 'personal-fixture-no-secret' not in json.dumps(value)
        writer.close();await writer.wait_closed()
        assert not (tmp_path/'quota.json').exists()
