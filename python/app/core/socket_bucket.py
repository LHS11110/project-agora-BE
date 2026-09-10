import time
import asyncio
from typing import Optional

# 기본 전송 대역폭 제한 규격 (사용자 요구사항)
DEFAULT_RECV_BYTE_LIMIT: float = 30000.0   # 한 명당 수신: 초당 30,000 Bytes (30 KB/s)
DEFAULT_SEND_BYTE_LIMIT: float = 900000.0  # 한 명당 송신: 초당 900,000 Bytes (900 KB/s)


class SocketBucket:
    """
    소켓 버켓 (토큰 버킷 기반 바이트 단위 Rate Limiter)
    - 사용자별 송수신 데이터 용량(Byte) 및 빈도를 비동기적으로 제어
    - 버스트(burst)는 capacity까지 허용하며, 지속 전송률은 refill_rate(Bytes/sec)로 제어
    """

    def __init__(self, capacity: float, refill_rate: float):
        """
        :param capacity: 버켓 최대 바이트 용량
        :param refill_rate: 초당 충전되는 바이트 수 (Bytes/sec)
        """
        self.capacity: float = float(capacity)
        self.refill_rate: float = float(refill_rate)
        self.tokens: float = float(capacity)
        self.last_refill: float = time.monotonic()
        self._lock: Optional[asyncio.Lock] = None

    def _get_lock(self) -> asyncio.Lock:
        if self._lock is None:
            self._lock = asyncio.Lock()
        return self._lock

    def _refill(self, now: float) -> None:
        elapsed = now - self.last_refill
        if elapsed > 0:
            self.tokens = min(self.capacity, self.tokens + elapsed * self.refill_rate)
            self.last_refill = now

    async def acquire(self, tokens: float = 1.0) -> bool:
        """
        지정된 크기(Byte)의 토큰을 소비합니다.
        토큰이 충분하면 True를 반환하고 토큰을 차감하며, 부족하면 False를 반환합니다.
        """
        async with self._get_lock():
            now = time.monotonic()
            self._refill(now)
            if self.tokens >= tokens:
                self.tokens -= tokens
                return True
            return False

    def can_acquire(self, tokens: float = 1.0) -> bool:
        """현재 토큰 소비 가능 여부 확인 (토큰 차감 없음)"""
        now = time.monotonic()
        elapsed = now - self.last_refill
        current_tokens = min(self.capacity, self.tokens + max(0.0, elapsed) * self.refill_rate)
        return current_tokens >= tokens

    def get_remaining_tokens(self) -> float:
        """현재 남아있는 사용 가능 바이트 수 반환"""
        now = time.monotonic()
        elapsed = now - self.last_refill
        return min(self.capacity, self.tokens + max(0.0, elapsed) * self.refill_rate)

    def reset(self) -> None:
        """버켓을 최대 용량으로 리셋"""
        self.tokens = self.capacity
        self.last_refill = time.monotonic()
