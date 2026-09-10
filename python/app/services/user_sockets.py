import asyncio
import json
import logging
import uuid
import time
from typing import Dict, List, Optional, Set, Any, Union
from starlette.websockets import WebSocket, WebSocketDisconnect

from app.core.socket_bucket import (
    SocketBucket,
    DEFAULT_RECV_BYTE_LIMIT,
    DEFAULT_SEND_BYTE_LIMIT
)

logger = logging.getLogger("app.services.user_sockets")


class SocketBucketException(Exception):
    """소켓 버켓 관련 예외 기본 클래스"""
    pass


class RateLimitExceededException(SocketBucketException):
    """소켓 버켓 송수신 용량(초당 Byte) 제한 초과 예외"""
    pass


class InvalidJsonException(SocketBucketException):
    """송수신 데이터의 JSON 규격 위반 예외"""
    pass


class CanvasFullException(SocketBucketException):
    """캔버스 최대 인원(20명) 초과 예외"""
    pass


class UserSocketSession:
    """
    개별 사용자의 WebSocket 연결 세션
    - 비동기 송수신 전담
    - 한 명당 수신: 초당 30,000 Bytes (30,000 B/s) 제한
    - 한 명당 송신: 초당 900,000 Bytes (900,000 B/s) 제한
    - 송수신되는 모든 데이터의 JSON 규격 보장
    """

    def __init__(
        self,
        session_id: str,
        user_id: Union[int, str],
        websocket: WebSocket,
        canvas_id: int,
        recv_limit_bytes: float = DEFAULT_RECV_BYTE_LIMIT,
        send_limit_bytes: float = DEFAULT_SEND_BYTE_LIMIT
    ):
        self.session_id: str = session_id
        self.user_id: Union[int, str] = user_id
        self.websocket: WebSocket = websocket
        self.canvas_id: int = canvas_id
        self.connected_at: float = time.time()

        # 사용자별 수신/송신 바이트 버켓 (수신 30,000 B/s, 송신 900,000 B/s)
        self.recv_bucket: SocketBucket = SocketBucket(
            capacity=recv_limit_bytes,
            refill_rate=recv_limit_bytes
        )
        self.send_bucket: SocketBucket = SocketBucket(
            capacity=send_limit_bytes,
            refill_rate=send_limit_bytes
        )

    # 하위 호환 별칭
    @property
    def bucket(self) -> SocketBucket:
        return self.recv_bucket

    async def send_json(self, data: Dict[str, Any]) -> bool:
        """
        비동기 방식으로 JSON 데이터를 클라이언트에게 송신합니다.
        - 송신 용량 제한: 초당 900,000 Bytes
        - 데이터는 반드시 dict(JSON 객체) 형태여야 합니다.
        """
        try:
            if not isinstance(data, dict):
                raise InvalidJsonException("송신 데이터는 반드시 dict/JSON 객체여야 합니다.")

            # JSON 직렬화 및 바이트 크기 계산
            json_text = json.dumps(data, ensure_ascii=False)
            byte_size = len(json_text.encode("utf-8"))

            # 송신 버켓 검사 (초당 900,000 B)
            allowed = await self.send_bucket.acquire(byte_size)
            if not allowed:
                logger.warning(
                    "[Session %s / User %s] 송신 대역폭 한도(900,000 B/s) 초과 (메시지 크기: %d B)",
                    self.session_id, self.user_id, byte_size
                )
                return False

            await self.websocket.send_text(json_text)
            return True
        except WebSocketDisconnect:
            raise
        except Exception as e:
            logger.warning("소켓 송신 실패 (session=%s, user=%s): %s", self.session_id, self.user_id, e)
            return False

    async def receive_json(self) -> Dict[str, Any]:
        """
        비동기 방식으로 클라이언트로부터 JSON 데이터를 수신합니다.
        - 수신 용량 제한: 초당 30,000 Bytes
        - 모든 수신 데이터는 JSON Object 형식이어야 합니다.
        초과 또는 JSON 규격 위반 시 즉시 에러 JSON 응답을 전송하고 예외를 발생시킵니다.
        """
        raw_message = await self.websocket.receive_text()
        byte_size = len(raw_message.encode("utf-8"))

        # 1. 수신 버켓 검사 (초당 30,000 B)
        allowed = await self.recv_bucket.acquire(byte_size)
        if not allowed:
            error_response = {
                "type": "error",
                "code": "RECEIVE_BANDWIDTH_EXCEEDED",
                "message": f"초당 수신 한도(30,000 B/s)를 초과했습니다. (요청 크기: {byte_size} B)",
                "limit_bytes_per_sec": int(DEFAULT_RECV_BYTE_LIMIT),
                "remaining_bytes": int(self.recv_bucket.get_remaining_tokens()),
                "timestamp": time.time()
            }
            try:
                await self.websocket.send_text(json.dumps(error_response, ensure_ascii=False))
            except Exception:
                pass
            raise RateLimitExceededException(f"Receive bandwidth exceeded: {byte_size} B")

        # 2. JSON 파싱 및 유효성 검증
        try:
            data = json.loads(raw_message)
            if not isinstance(data, dict):
                raise ValueError("JSON root must be an object")
            return data
        except Exception as e:
            error_response = {
                "type": "error",
                "code": "INVALID_JSON",
                "message": "송수신되는 데이터는 모두 유효한 JSON 형식(JSON Object)이어야 합니다.",
                "detail": str(e),
                "timestamp": time.time()
            }
            try:
                await self.websocket.send_text(json.dumps(error_response, ensure_ascii=False))
            except Exception:
                pass
            raise InvalidJsonException(f"Invalid JSON received: {e}")


class UserSockets:
    """
    특정 Canvas 내 사용자들의 WebSocket 연결을 관리하는 비동기 세션 관리자
    - 캔버스 하나당 최대 20명 제한
    - 사용자별 수신(30,000 B/s), 송신(900,000 B/s) 소켓 버켓 관리
    - 모든 송수신 데이터 엄격한 JSON 규격 보장
    """

    def __init__(self, canvas_id: int, max_users: int = 20):
        self.canvas_id: int = canvas_id
        self.max_users: int = max_users
        self._sessions: Dict[str, UserSocketSession] = {}
        self._user_to_sessions: Dict[Union[int, str], Set[str]] = {}
        self._lock: asyncio.Lock = asyncio.Lock()

    async def connect(
        self,
        websocket: WebSocket,
        user_id: Union[int, str],
        recv_limit_bytes: float = DEFAULT_RECV_BYTE_LIMIT,
        send_limit_bytes: float = DEFAULT_SEND_BYTE_LIMIT
    ) -> UserSocketSession:
        """
        새로운 사용자 WebSocket 연결을 수락하고 세션을 등록합니다.
        캔버스당 최대 20명 제한을 검증합니다.
        """
        async with self._lock:
            # 캔버스 하나당 최대 20명 검증 (동일 사용자의 다중 탭은 허용, 신규 사용자는 20명 제한)
            current_users = list(self._user_to_sessions.keys())
            if len(current_users) >= self.max_users and user_id not in self._user_to_sessions:
                error_response = {
                    "type": "error",
                    "code": "CANVAS_FULL",
                    "message": f"캔버스 최대 동시 접속 인원({self.max_users}명)을 초과했습니다.",
                    "max_users": self.max_users,
                    "active_users_count": len(current_users),
                    "timestamp": time.time()
                }
                # WebSocket 수락 후 에러 JSON 전송 및 닫기
                await websocket.accept()
                await websocket.send_text(json.dumps(error_response, ensure_ascii=False))
                await websocket.close(code=4003, reason=f"Canvas full (max {self.max_users} users)")
                raise CanvasFullException(f"Canvas #{self.canvas_id} reached maximum user limit of {self.max_users}")

            await websocket.accept()

            session_id = str(uuid.uuid4())
            session = UserSocketSession(
                session_id=session_id,
                user_id=user_id,
                websocket=websocket,
                canvas_id=self.canvas_id,
                recv_limit_bytes=recv_limit_bytes,
                send_limit_bytes=send_limit_bytes
            )

            self._sessions[session_id] = session
            if user_id not in self._user_to_sessions:
                self._user_to_sessions[user_id] = set()
            self._user_to_sessions[user_id].add(session_id)

        logger.info("[Canvas #%d] User %s connected (session: %s, active users: %d/%d)",
                    self.canvas_id, user_id, session_id, len(self._user_to_sessions), self.max_users)

        # 연결 성공 환영 JSON 전송
        await session.send_json({
            "type": "connected",
            "canvas_id": self.canvas_id,
            "user_id": user_id,
            "session_id": session_id,
            "max_users": self.max_users,
            "active_users": self.get_active_users(),
            "limits": {
                "recv_bytes_per_sec": int(recv_limit_bytes),
                "send_bytes_per_sec": int(send_limit_bytes)
            },
            "timestamp": time.time()
        })

        return session

    async def disconnect(self, session_id: str) -> Optional[UserSocketSession]:
        """사용자 세션 연결 해제 및 관리 목록에서 제거"""
        async with self._lock:
            session = self._sessions.pop(session_id, None)
            if session:
                user_id = session.user_id
                if user_id in self._user_to_sessions:
                    self._user_to_sessions[user_id].discard(session_id)
                    if not self._user_to_sessions[user_id]:
                        del self._user_to_sessions[user_id]

        if session:
            logger.info("[Canvas #%d] User %s disconnected (session: %s, remaining users: %d)",
                        self.canvas_id, session.user_id, session_id, len(self._user_to_sessions))
        return session

    async def broadcast_json(
        self,
        data: Dict[str, Any],
        exclude_session_id: Optional[str] = None
    ) -> int:
        """
        캔버스 내 모든 활성 소켓에 비동기 JSON 브로드캐스트 전송
        """
        if not isinstance(data, dict):
            raise InvalidJsonException("브로드캐스트 데이터는 반드시 dict/JSON 객체여야 합니다.")

        async with self._lock:
            targets = [
                sess for s_id, sess in self._sessions.items()
                if exclude_session_id is None or s_id != exclude_session_id
            ]

        if not targets:
            return 0

        results = await asyncio.gather(
            *(sess.send_json(data) for sess in targets),
            return_exceptions=True
        )

        return sum(1 for r in results if r is True)

    async def send_to_user_json(
        self,
        user_id: Union[int, str],
        data: Dict[str, Any]
    ) -> int:
        """특정 사용자의 모든 활성 세션에 비동기 JSON 전송"""
        if not isinstance(data, dict):
            raise InvalidJsonException("유저 전송 데이터는 반드시 dict/JSON 객체여야 합니다.")

        async with self._lock:
            session_ids = list(self._user_to_sessions.get(user_id, set()))
            targets = [self._sessions[s_id] for s_id in session_ids if s_id in self._sessions]

        if not targets:
            return 0

        results = await asyncio.gather(
            *(sess.send_json(data) for sess in targets),
            return_exceptions=True
        )
        return sum(1 for r in results if r is True)

    def get_active_users(self) -> List[Union[int, str]]:
        """접속 중인 고유 사용자 ID 목록 반환"""
        return list(self._user_to_sessions.keys())

    def get_connection_count(self) -> int:
        """활성화된 총 소켓 연결 수 반환"""
        return len(self._sessions)

    async def close_all(self, code: int = 1000, reason: str = "Canvas closed") -> None:
        """캔버스 소속 모든 소켓 세션 정상 종료"""
        async with self._lock:
            sessions = list(self._sessions.values())
            self._sessions.clear()
            self._user_to_sessions.clear()

        close_message = {
            "type": "closed",
            "canvas_id": self.canvas_id,
            "reason": reason,
            "timestamp": time.time()
        }

        for sess in sessions:
            try:
                await sess.send_json(close_message)
                await sess.websocket.close(code=code, reason=reason)
            except Exception:
                pass
        logger.info("[Canvas #%d] Closed all %d user socket connections", self.canvas_id, len(sessions))
