import logging
from typing import Dict, List, Optional, Any
from app.models.canvas import Canvas, CanvasItem, CreateCanvasRequest
from app.services.spring_client import spring_client
from app.services.redis_service import redis_service
from app.services.canvas_manager import canvas_manager

logger = logging.getLogger("app.services.canvas_service")


class CanvasService:
    """
    캔버스 비즈니스 로직 서비스
    - 정책: Python 서버는 Redis 외 데이터베이스(MSSQL, Elasticsearch)에 직접 접근하지 않음
    - 모든 데이터베이스 연동 및 저장은 Spring Boot API(spring_client)를 통해 수행
    """

    def __init__(self):
        self._next_id: int = 1

    def create_canvas(self, req: CreateCanvasRequest) -> Canvas:
        """
        신규 캔버스 생성 및 Spring API를 통한 도큐먼트 저장
        """
        canvas_id = self._next_id
        self._next_id += 1

        admin_uid = req.admin
        init_group = req.init_group or "default"

        inner_group: Dict[str, List[int]] = {
            "admin-group": [admin_uid]
        }
        if init_group not in inner_group:
            inner_group[init_group] = []

        canvas = Canvas(
            **{
                "canvas-name": req.canvas_name,
                "canvas-id": canvas_id,
                "admin": admin_uid,
                "peoples": [admin_uid],
                "inner-group": inner_group,
                "items": {},
                "init-group": init_group
            }
        )

        # 런타임 캔버스 매니저에 등록
        canvas_manager.get_or_create_canvas(
            canvas_id=canvas_id,
            canvas_name=req.canvas_name,
            admin=admin_uid,
            init_group=init_group
        )

        return canvas

    def get_canvas_by_name(self, canvas_name: str) -> Optional[Canvas]:
        """Spring API를 통해 이름(canvas-name)으로 캔버스 도큐먼트를 조회합니다."""
        data = spring_client.get_canvas_document_by_name(canvas_name)
        if data:
            try:
                return Canvas(**data)
            except Exception as e:
                logger.warning("Failed to deserialize canvas document '%s': %s", canvas_name, e)
                return None
        return None

    def get_canvas(self, canvas_id: int) -> Optional[Canvas]:
        """Spring API를 통해 ID로 캔버스 도큐먼트를 조회합니다."""
        data = spring_client.get_canvas_document(canvas_id)
        if data:
            try:
                return Canvas(**data)
            except Exception as e:
                logger.warning("Failed to deserialize canvas document #%d: %s", canvas_id, e)
                return None

        # Spring API에 없으면 런타임 캐시 확인
        runtime_canvas = canvas_manager.get_canvas(canvas_id)
        if runtime_canvas:
            return Canvas(**runtime_canvas.to_dict())
        return None

    def list_canvases(self) -> List[Canvas]:
        """Spring API를 통해 전체 캔버스 도큐먼트 목록을 조회합니다."""
        data_list = spring_client.list_canvas_documents()
        canvases: List[Canvas] = []
        for item in data_list:
            try:
                canvases.append(Canvas(**item))
            except Exception:
                continue
        return canvases

    def join_user(self, canvas_id: int, uid: int) -> Optional[Canvas]:
        """캔버스에 사용자를 참가시키고 Spring API와 동기화합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        if uid not in canvas.peoples:
            canvas.peoples.append(uid)

        init_grp = canvas.init_group
        if init_grp not in canvas.inner_group:
            canvas.inner_group[init_grp] = []

        if uid not in canvas.inner_group[init_grp]:
            canvas.inner_group[init_grp].append(uid)

        # Spring API로 도큐먼트 업데이트
        spring_client.update_canvas_document(canvas_id, canvas.model_dump(by_alias=True))
        return canvas

    def add_inner_group(self, canvas_id: int, group_name: str) -> Optional[Canvas]:
        """새로운 내부 그룹을 추가하고 Spring API와 동기화합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        if group_name not in canvas.inner_group:
            canvas.inner_group[group_name] = []
            spring_client.update_canvas_document(canvas_id, canvas.model_dump(by_alias=True))

        return canvas

    def move_user_group(self, canvas_id: int, uid: int, target_group: str) -> Optional[Canvas]:
        """사용자의 소속 내부 그룹을 변경하고 Spring API와 동기화합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas or uid not in canvas.peoples:
            return None

        if target_group not in canvas.inner_group:
            canvas.inner_group[target_group] = []

        for g_name, uids in canvas.inner_group.items():
            if g_name != target_group and uid in uids:
                uids.remove(uid)

        if uid not in canvas.inner_group[target_group]:
            canvas.inner_group[target_group].append(uid)

        spring_client.update_canvas_document(canvas_id, canvas.model_dump(by_alias=True))
        return canvas

    def put_item(self, canvas_id: int, item_id: str, item: CanvasItem) -> Optional[Canvas]:
        """캔버스에 아이템을 배치/수정하고 Spring API와 동기화합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        for g_name, perm in list(item.permission.items()):
            item.permission[g_name] = max(0, min(7, perm))
        item.permission["admin-group"] = 7
        canvas.items[item_id] = item

        spring_client.update_canvas_document(canvas_id, canvas.model_dump(by_alias=True))
        return canvas

    def remove_item(self, canvas_id: int, item_id: str) -> Optional[Canvas]:
        """캔버스에서 특정 아이템을 제거하고 Spring API와 동기화합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas or item_id not in canvas.items:
            return None

        del canvas.items[item_id]
        spring_client.update_canvas_document(canvas_id, canvas.model_dump(by_alias=True))
        return canvas

    def delete_canvas(self, canvas_id: int) -> bool:
        """캔버스 삭제 처리 (런타임 정리 및 Redis 삭제)"""
        runtime_canvas = canvas_manager.get_canvas(canvas_id)
        if runtime_canvas:
            redis_service.delete_canvas_cache(runtime_canvas.Redis_ip, runtime_canvas.Redis_port, canvas_id)
        return True

    def delete_canvas_by_name(self, canvas_name: str) -> bool:
        """이름으로 캔버스 삭제"""
        canvas = self.get_canvas_by_name(canvas_name)
        if canvas:
            return self.delete_canvas(canvas.canvas_id)
        return False

    async def delete_canvas_from_server_and_redis(
        self,
        canvas_id: int,
        redis_ip: Optional[str] = None,
        redis_port: Optional[str] = None
    ) -> Dict[str, Any]:
        """
        Python 서버의 메모리/소켓 세션 및 Redis 캐시에서 캔버스 데이터를 일괄 제거합니다.
        (직접 DB 접근 금지: Redis 외 데이터 정리는 Spring에서 전담 처리)
        """
        # 1. 런타임 활성 캔버스 및 WebSocket 세션 일괄 종료/정리
        await canvas_manager.remove_canvas(canvas_id)
        server_removed = True
        redis_removed = False

        # 2. Redis 캐시 키 일괄 삭제
        if redis_ip and redis_port:
            try:
                port_num = int(redis_port)
                redis_removed = redis_service.delete_canvas_cache(redis_ip, port_num, canvas_id)
            except Exception as e:
                logger.warning("Redis cleanup failed on %s:%s: %s", redis_ip, redis_port, e)
                redis_removed = False
        else:
            try:
                redis_removed = redis_service.delete_canvas_cache("127.0.0.1", 6379, canvas_id)
            except Exception:
                redis_removed = False

        return {
            "status": "success",
            "canvas_id": canvas_id,
            "server_removed": server_removed,
            "redis_removed": redis_removed,
            "message": f"Canvas {canvas_id} removed from server memory and Redis cache"
        }


# 싱글톤 인스턴스
canvas_service = CanvasService()
