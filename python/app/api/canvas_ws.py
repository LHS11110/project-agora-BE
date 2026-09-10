import logging
import time
from typing import Optional, Union, Dict, Any
from fastapi import APIRouter, WebSocket, WebSocketDisconnect, Query, HTTPException, status

from app.services.canvas_manager import canvas_manager
from app.services.user_sockets import RateLimitExceededException, InvalidJsonException

logger = logging.getLogger("app.api.canvas_ws")

router = APIRouter(
    tags=["Canvas WebSocket"]
)


@router.websocket("/ws/canvas/{canvas_id}")
async def canvas_websocket_endpoint(
    websocket: WebSocket,
    canvas_id: int,
    user_id: Optional[str] = Query(None, description="접속 사용자 고유 식별자 (ID 또는 닉네임)")
):
    """
    캔버스 실시간 협업 WebSocket 엔드포인트
    - 요구사항:
      1. Canvas 내 UserSockets(비동기 송수신, 소켓 버켓 사용자별 송수신 횟수 제한) 관리
      2. 송수신되는 모든 데이터는 엄격한 JSON 형식 준수
    """
    uid: Union[int, str] = user_id if user_id is not None and user_id != "" else "anonymous"
    try:
        uid = int(uid)
    except ValueError:
        pass

    # 활성 Canvas 인스턴스 조회 또는 등록 (Redis_ip, Redis_port 기본 포함)
    canvas = canvas_manager.get_or_create_canvas(canvas_id=canvas_id)

    # 1. 비동기 UserSockets에 신규 세션 연결 및 소켓 버켓 등록
    session = await canvas.UserSockets.connect(
        websocket=websocket,
        user_id=uid
    )

    try:
        # 2. 비동기 실시간 메시지 송수신 루프
        while True:
            try:
                # 비동기 JSON 수신 (소켓 버켓 횟수 제한 및 JSON 파싱 검증 포함)
                message_data = await session.receive_json()
            except RateLimitExceededException:
                # 소켓 버켓 초과 시 안내 메시지가 session.receive_json() 내부에서 클라이언트로 자동 전송됨
                continue
            except InvalidJsonException:
                # 비-JSON 형식 수신 시 에러 메시지가 session.receive_json() 내부에서 클라이언트로 자동 전송됨
                continue

            # 메시지 액션 처리
            msg_type = message_data.get("type", "broadcast")

            if msg_type == "ping":
                await session.send_json({
                    "type": "pong",
                    "canvas_id": canvas_id,
                    "timestamp": time.time()
                })
                continue

            # 브로드캐스트 데이터 구성 (발신자 정보 포함)
            out_data: Dict[str, Any] = dict(message_data)
            out_data["sender_id"] = uid
            out_data["canvas_id"] = canvas_id
            if "timestamp" not in out_data:
                out_data["timestamp"] = time.time()

            # 캔버스 내 다른 접속자들에게 비동기 브로드캐스트 전송
            await canvas.UserSockets.broadcast_json(
                out_data,
                exclude_session_id=session.session_id
            )

    except WebSocketDisconnect:
        logger.info("[Canvas #%d] WebSocket disconnected normally for user %s (session %s)",
                    canvas_id, uid, session.session_id)
    except Exception as e:
        logger.warning("[Canvas #%d] WebSocket error for user %s: %s", canvas_id, uid, e)
    finally:
        # 연결 해제 처리 및 퇴장 알림 브로드캐스트
        await canvas.UserSockets.disconnect(session.session_id)
        try:
            await canvas.UserSockets.broadcast_json({
                "type": "user_left",
                "canvas_id": canvas_id,
                "user_id": uid,
                "active_users": canvas.UserSockets.get_active_users(),
                "remaining_connections": canvas.UserSockets.get_connection_count(),
                "timestamp": time.time()
            })
        except Exception:
            pass


@router.get(
    "/api/canvas/{canvas_id}/runtime",
    summary="활성 캔버스 런타임 상태 조회 (UserSockets, Redis 정보)",
    tags=["Canvas"]
)
def get_canvas_runtime_status(canvas_id: int) -> Dict[str, Any]:
    """
    Python 서버에서 관리 중인 Canvas 인스턴스(UserSockets 접속 현황, Redis_ip, Redis_port)를 조회합니다.
    """
    canvas = canvas_manager.get_canvas(canvas_id)
    if not canvas:
        raise HTTPException(
            status_code=status.HTTP_404_NOT_FOUND,
            detail=f"Active canvas with id {canvas_id} not found in runtime"
        )
    return canvas.to_dict()
