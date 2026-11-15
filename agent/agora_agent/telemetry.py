"""Only numeric, allowlisted provider quota data; never headers with credentials."""
import copy
import hashlib
import json
import math
import os
import time
from pathlib import Path
from contextvars import ContextVar
from collections import OrderedDict

_state = None
_personal = OrderedDict()
_identity = ContextVar('agent_quota_identity', default=None)

def bind(api_key):
    identity = hashlib.sha256(api_key.encode()).hexdigest() if api_key and api_key != os.getenv('AGENT_API_KEY', '') else None
    return _identity.set(identity)

def unbind(token):
    _identity.reset(token)


def _path():
    return Path(os.getenv('AGENT_SOCKET', '/run/agora-agent/agent.sock')).parent / 'quota.json'

def _key():
    return hashlib.sha256(os.getenv('AGENT_API_KEY', '').encode()).hexdigest()

def snapshot():
    global _state
    identity = _identity.get()
    if identity is not None:
        if identity in _personal:
            _personal.move_to_end(identity)
        return copy.deepcopy(_personal.get(identity, {}))
    if _state is None:
        _state = {}
        try:
            saved = json.loads(_path().read_text())
            if saved.get('key') == _key():
                _state = saved['telemetry']
        except (OSError, ValueError, KeyError, TypeError):
            pass
    return copy.deepcopy(_state)

def _number(value):
    try:
        number = float(value)
        return number if math.isfinite(number) and 0 <= number <= 1e12 else None
    except (TypeError, ValueError):
        return None

def record(headers, status):
    global _state
    previous = snapshot()
    limits = {}
    for window in ('second', 'minute', 'day'):
        limit = _number(headers.get(f'x-{window}-rate-limit'))
        remaining = _number(headers.get(f'x-{window}-rate-limit-remaining'))
        if limit is not None or remaining is not None:
            limits[window] = {'limit': limit, 'remaining': remaining}
    observed = time.time()
    retry_ms = _number(headers.get('retry-after-ms'))
    retry_seconds = _number(headers.get('retry-after'))
    retry = retry_ms / 1000 if retry_ms is not None else retry_seconds
    current = {'limits': limits or previous.get('limits', {}), 'observed_at': observed,
              'limits_observed_at': observed if limits else previous.get('limits_observed_at'),
              'status': status, 'retry_at': observed + retry if retry is not None else None}
    identity = _identity.get()
    if identity is not None:
        _personal[identity] = current
        _personal.move_to_end(identity)
        while len(_personal) > 64:
            _personal.popitem(last=False)
        return snapshot()
    _state = current
    try:
        temporary = _path().with_suffix('.tmp')
        # API calls are asynchronous but this small atomic write has no await.
        temporary.write_text(json.dumps({'key': _key(), 'telemetry': _state}))
        temporary.chmod(0o600)
        temporary.replace(_path())
    except OSError:
        pass
    return snapshot()
