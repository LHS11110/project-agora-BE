"""Server-owned model provider registry.

Requests choose only a provider id and a model name. Endpoints are fixed here, so a
personal key can never be sent to a URL that came from a request.
"""
import os
import re


def canvas_external_models():
    return {model for model in os.getenv("AGENT_EXTERNAL_MODELS", "").split(",")
            if model.startswith("anthropic/claude-") and valid_model(model)}

PROVIDERS = {
    # EXAONE was removed: its API terms (2026-11-09, 2.1/2.5) allow non-commercial use
    # only and forbid integrating the API into products or letting third parties call it.
    "openai": {
        "label": "OpenAI",
        "kind": "openai",
        "base_url": "https://api.openai.com/v1",
        "model": "gpt-5",
        "key_env": "AGENT_OPENAI_API_KEY",
    },
    # Claude runs through the official Anthropic SDK (anthropic_engine.py).
    "anthropic": {
        "label": "Anthropic Claude",
        "kind": "anthropic",
        "model": "claude-opus-5-5",
        "key_env": "AGENT_ANTHROPIC_API_KEY",
    },
    "gemini": {
        "label": "Google Gemini",
        "kind": "openai-compatible",
        "base_url": "https://generativelanguage.googleapis.com/v1beta/openai/",
        "model": "gemini-2.5-flash",
        "key_env": "AGENT_GEMINI_API_KEY",
    },
    "openrouter": {
        "label": "OpenRouter",
        "kind": "openai-compatible",
        "base_url": "https://openrouter.ai/api/v1",
        "model": "openrouter/auto",
        "key_env": "AGENT_OPENROUTER_API_KEY",
    },
}

DEFAULT_PROVIDER = "openai"
MODEL_PATTERN = re.compile(r"[A-Za-z0-9][A-Za-z0-9._:/@-]{0,127}")


def valid_provider(value):
    return isinstance(value, str) and value in PROVIDERS


def valid_model(value):
    # An empty model selects the provider default.
    return isinstance(value, str) and (value == "" or MODEL_PATTERN.fullmatch(value) is not None)


def spec(provider_id):
    return {"id": provider_id, **PROVIDERS[provider_id]}


def base_url(provider):
    return provider["base_url"]


def model_for(provider, requested=""):
    return requested or provider["model"]


def server_key(provider):
    return os.getenv(provider["key_env"], "")
