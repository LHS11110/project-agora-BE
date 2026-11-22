"""Per-request model options; endpoints and server keys stay server-owned."""
import math
import os
from .providers import DEFAULT_PROVIDER, valid_model, valid_provider

RANGES = {'max_tokens': (512, 32768), 'max_steps': (1, 12), 'temperature': (0, 2), 'top_p': (.05, 1)}

def defaults():
    return {'reasoning_effort': os.getenv('AGENT_REASONING_EFFORT', 'low'),
            'max_tokens': int(os.getenv('AGENT_MAX_TOKENS', '8192')),
            'max_steps': int(os.getenv('AGENT_MAX_STEPS', '8')), 'temperature': 1, 'top_p': .95,
            'provider': DEFAULT_PROVIDER, 'model': ''}

def options(value):
    if not isinstance(value, dict) or set(value) - (set(RANGES) | {'reasoning_effort', 'provider', 'model'}):
        raise ValueError('AGENT_OPTIONS_INVALID')
    result = defaults()
    result.update(value)
    if result['reasoning_effort'] not in ('low', 'high', 'max'):
        raise ValueError('AGENT_OPTIONS_INVALID')
    if not valid_provider(result['provider']) or not valid_model(result['model']):
        raise ValueError('AGENT_OPTIONS_INVALID')
    for name, (lower, upper) in RANGES.items():
        number = result[name]
        if isinstance(number, bool) or not isinstance(number, (int, float)) or not math.isfinite(number) or not lower <= number <= upper:
            raise ValueError('AGENT_OPTIONS_INVALID')
        if name in ('max_tokens', 'max_steps') and not isinstance(number, int):
            raise ValueError('AGENT_OPTIONS_INVALID')
    return result
