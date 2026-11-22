"""Claude provider: the official Anthropic SDK driving the same MCP tool session.

The loop is written by hand (not the SDK tool runner) so it keeps the engine's step
limit, progress messages, tool result size limit and error wrapping.
"""
import json
from anthropic import AsyncAnthropic
from .engine import EMPTY_ANSWER, LIMIT_ANSWER, MAX_CONTEXT_CHARS, STEP_LIMIT_ANSWER, run_tool

# Models that accept output_config.effort (low/high/max map directly).
EFFORT_MODEL_PREFIXES = (
    "claude-fable-", "claude-mythos-", "claude-opus-5", "claude-opus-4-8", "claude-opus-4-7",
    "claude-opus-4-6", "claude-sonnet-5", "claude-sonnet-4-6", "claude-haiku-5",
)
# Models with server-side refusal fallbacks; "default" routes by refusal category.
FALLBACK_MODELS = {"claude-fable-5-2", "claude-fable-5-1", "claude-opus-5-5", "claude-opus-5", "claude-sonnet-5-5"}
FALLBACK_BETA = "server-side-fallback-2026-07-01"
REFUSAL_ANSWER = "모델이 이 요청을 처리하지 않기로 했습니다. 요청 내용을 바꾸거나 다른 모델을 선택해주세요."
JSON_RETRIES = 2


def create_client(key):
    # Streaming avoids HTTP timeouts at high max_tokens; the SDK retries 429/5xx itself.
    return AsyncAnthropic(api_key=key, timeout=120, max_retries=2)


def claude_tools(mcp_tools):
    return [
        {
            "name": tool.name,
            "description": tool.description or "",
            "input_schema": tool.inputSchema,
            # Code bodies can be large; stream them as generated. The input is then
            # not validated by the API, so the loop checks it before running a tool.
            "eager_input_streaming": True,
        }
        for tool in mcp_tools
    ]


def request_params(model, config, system, messages, tools):
    params = {
        "model": model,
        "max_tokens": config["max_tokens"],
        "system": system,
        "messages": messages,
        "tools": tools,
    }
    # Temperature/top_p are not sent; current Claude models reject them.
    if model.startswith(EFFORT_MODEL_PREFIXES):
        params["output_config"] = {"effort": config["reasoning_effort"]}
    if model in FALLBACK_MODELS:
        params["betas"] = [FALLBACK_BETA]
        params["fallbacks"] = "default"
    return params


async def create_message(client, params):
    async with client.beta.messages.stream(**params) as stream:
        return await stream.get_final_message()


def text_of(message):
    return "".join(block.text for block in message.content if block.type == "text").strip()


def opening_messages(entries):
    # The Messages API starts with a user turn; history may begin with an answer.
    while entries and entries[0]["role"] != "user":
        entries = entries[1:]
    return list(entries)


async def claude_loop(client, model, config, system, entries, mcp_tools, session, progress):
    messages = opening_messages(entries)
    tools = claude_tools(mcp_tools)
    size = sum(len(entry["content"]) for entry in messages)
    json_failures = 0
    for step in range(config["max_steps"]):
        await progress(f"모델이 작업을 계획하고 있습니다. ({step+1})\n")
        try:
            response = await create_message(client, request_params(model, config, system, messages, tools))
        except ValueError:
            # Tool input JSON the SDK could not parse at all: no tool_use id exists
            # to answer, so the turn is re-issued (bounded). API errors propagate.
            json_failures += 1
            if json_failures > JSON_RETRIES:
                raise
            continue
        json_failures = 0
        served_by = response.model or model
        if response.stop_reason == "refusal":
            return {"answer": text_of(response) or REFUSAL_ANSWER, "model": served_by}
        if response.stop_reason == "max_tokens":
            # A tool input cut off at the cap is never run.
            return {"answer": (text_of(response) + "\n\n" + LIMIT_ANSWER).strip(), "limited": True, "model": served_by}
        # Thinking and fallback blocks go back unchanged, as the API requires.
        messages.append({"role": "assistant", "content": response.content})
        size += sum(len(block.model_dump_json()) for block in response.content)
        if response.stop_reason == "pause_turn":
            continue
        tool_uses = [block for block in response.content if block.type == "tool_use"]
        if not tool_uses:
            return {"answer": text_of(response) or EMPTY_ANSWER, "model": served_by}
        if len(tool_uses) > 10:
            raise ValueError("Too many tool calls")
        results = []
        for block in tool_uses:
            if isinstance(block.input, dict):
                results.append({"type": "tool_result", "tool_use_id": block.id, "content": await run_tool(session, progress, block.name, block.input)})
            else:
                results.append({
                    "type": "tool_result",
                    "tool_use_id": block.id,
                    "is_error": True,
                    "content": json.dumps({"INVALID_JSON": str(block.input)}, ensure_ascii=False),
                })
        # All results of one turn go back in a single user message.
        messages.append({"role": "user", "content": results})
        size += sum(len(result["content"]) for result in results)
        if size > MAX_CONTEXT_CHARS:
            raise ValueError("Context limit")
    return {"answer": STEP_LIMIT_ANSWER, "limited": True}
