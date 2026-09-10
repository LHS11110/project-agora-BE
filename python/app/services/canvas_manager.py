import logging
from typing import Dict, Optional, List
from app.models.canvas_runtime import Canvas

logger = logging.getLogger("app.services.canvas_manager")


class CanvasManager:
    """
    Python 서버 내 활성 Canvas 인스턴스들의 생명주기를 총괄 관리하는 싱글톤 매니저
    """

    def __init__(self):
        self._canvases: Dict[int, Canvas] = {}

    def get_or_create_canvas(
        self,
        canvas_id: int,
        canvas_name: str = "",
        redis_ip: str = "127.0.0.1",
        redis_port: int = 6379,
        admin: Optional[int] = None,
        init_group: str = "default"
    ) -> Canvas:
        """활성 캔버스를 반환하거나, 없으면 신규 생성하여 등록합니다."""
        if canvas_id in self._canvases:
            canvas = self._canvases[canvas_id]
            if redis_ip:
                canvas.Redis_ip = redis_ip
            if redis_port:
                canvas.Redis_port = int(redis_port)
            if canvas_name and not canvas.canvas_name:
                canvas.canvas_name = canvas_name
            return canvas

        canvas = Canvas(
            canvas_id=canvas_id,
            canvas_name=canvas_name or f"Canvas-{canvas_id}",
            redis_ip=redis_ip,
            redis_port=redis_port,
            admin=admin,
            init_group=init_group
        )
        self._canvases[canvas_id] = canvas
        logger.info("Created and registered active runtime Canvas #%d (Redis: %s:%d)",
                    canvas_id, redis_ip, redis_port)
        return canvas

    def get_canvas(self, canvas_id: int) -> Optional[Canvas]:
        """지정한 ID의 활성 캔버스를 반환합니다."""
        return self._canvases.get(canvas_id)

    async def remove_canvas(self, canvas_id: int) -> Optional[Canvas]:
        """
        캔버스를 런타임 관리 목록에서 제거하고, 소속된 모든 WebSocket 연결을 정상 종료합니다.
        """
        canvas = self._canvases.pop(canvas_id, None)
        if canvas:
            # 소속된 모든 사용자 소켓 세션 정상 종료
            await canvas.UserSockets.close_all(reason="Canvas has been deleted or unallocated")
            logger.info("Removed active Canvas #%d and closed all its user sockets", canvas_id)
        return canvas

    def list_active_canvases(self) -> List[Canvas]:
        """현재 활성화된 모든 캔버스 목록 반환"""
        return list(self._canvases.values())

    def get_active_count(self) -> int:
        """활성화된 총 캔버스 수"""
        return len(self._canvases)


# 싱글톤 인스턴스
canvas_manager = CanvasManager()
