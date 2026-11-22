"""A personal key is used only for this request; never add it to model inputs."""
from .providers import DEFAULT_PROVIDER, server_key, spec

def personal_key(payload):
    value = payload.get('api_key', '')
    if not isinstance(value, str) or len(value) > 4096:
        raise ValueError('AGENT_API_KEY_INVALID')
    value = value.strip()
    if any(ord(char) < 33 or ord(char) > 126 for char in value):
        raise ValueError('AGENT_API_KEY_INVALID')
    return value

def effective_key(payload, provider_id=DEFAULT_PROVIDER):
    # A personal key belongs to the provider chosen in the same request.
    return personal_key(payload) or server_key(spec(provider_id))
