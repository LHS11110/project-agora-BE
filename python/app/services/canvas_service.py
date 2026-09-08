from typing import Dict, List, Optional
from app.models.canvas import Canvas, CanvasItem, CreateCanvasRequest
from app.services.es_service import es_service


class CanvasService:
    """캔버스 생성, 조회, 수정 및 관리 비즈니스 로직 서비스 (Elasticsearch 연동)"""

    def __init__(self):
        self._next_id: int = 1

    def create_canvas(self, req: CreateCanvasRequest) -> Canvas:
        """
        form.txt 최신 규격에 맞춰 새로운 캔버스를 생성하고 Elasticsearch에 색인/저장합니다.
        문서의 기본키는 canvas_name으로 사용됩니다.
        """
        # ES 내 최대 ID 조회 후 다음 ID 결정
        max_id = es_service.get_max_canvas_id()
        if max_id >= self._next_id:
            self._next_id = max_id + 1

        canvas_id = self._next_id
        self._next_id += 1

        admin_uid = req.admin
        init_group = req.init_group or "default"

        # 기본 내부 그룹 구성 (admin-group + 초기 배정 그룹)
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

        # Elasticsearch에 기본키(canvas-name)로 저장
        es_service.save_canvas(canvas)

        return canvas

    def get_canvas_by_name(self, canvas_name: str) -> Optional[Canvas]:
        """Elasticsearch 기본키(canvas-name)로 캔버스를 단건 조회합니다."""
        es_data = es_service.get_canvas_by_name(canvas_name)
        if es_data:
            try:
                return Canvas(**es_data)
            except Exception:
                return None
        return None

    def delete_canvas_by_name(self, canvas_name: str) -> bool:
        """기본키(canvas-name)로 Elasticsearch에서 캔버스를 삭제합니다."""
        return es_service.delete_canvas_by_name(canvas_name)

    def get_canvas(self, canvas_id: int) -> Optional[Canvas]:
        """ID로 Elasticsearch에서 캔버스를 단건 조회합니다."""
        es_data = es_service.get_canvas(canvas_id)
        if es_data:
            try:
                return Canvas(**es_data)
            except Exception:
                return None
        return None

    def list_canvases(self) -> List[Canvas]:
        """등록된 전체 캔버스 목록을 Elasticsearch에서 조회하여 반환합니다."""
        es_data_list = es_service.list_canvases()
        canvases: List[Canvas] = []
        for item in es_data_list:
            try:
                canvases.append(Canvas(**item))
            except Exception:
                continue
        return canvases

    def join_user(self, canvas_id: int, uid: int) -> Optional[Canvas]:
        """캔버스에 사용자를 참가시키고 초기 내부 그룹(init-group)에 배정합니다."""
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

        # ES 동기화
        es_service.save_canvas(canvas)
        return canvas

    def add_inner_group(self, canvas_id: int, group_name: str) -> Optional[Canvas]:
        """새로운 내부 그룹을 추가합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        if group_name not in canvas.inner_group:
            canvas.inner_group[group_name] = []
            es_service.save_canvas(canvas)

        return canvas

    def move_user_group(self, canvas_id: int, uid: int, target_group: str) -> Optional[Canvas]:
        """특정 사용자의 소속 내부 그룹을 변경합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas or uid not in canvas.peoples:
            return None

        if target_group not in canvas.inner_group:
            canvas.inner_group[target_group] = []

        # 기존 일반 그룹에서 제거 (단, admin-group 소속 여부는 유지 가능)
        for g_name, uids in canvas.inner_group.items():
            if g_name != target_group and uid in uids:
                uids.remove(uid)

        if uid not in canvas.inner_group[target_group]:
            canvas.inner_group[target_group].append(uid)

        # ES 동기화
        es_service.save_canvas(canvas)
        return canvas

    def put_item(self, canvas_id: int, item_id: str, item: CanvasItem) -> Optional[Canvas]:
        """
        캔버스에 아이템을 배치하거나 업데이트합니다.
        - permission 맵에 'admin-group': 7이 항상 포함되도록 보장
        - 변경 사항을 Elasticsearch에 반영
        """
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        # 모든 그룹 권한은 0~7 사이로 유지하며 admin-group 권한은 항상 7로 고정
        for g_name, perm in list(item.permission.items()):
            item.permission[g_name] = max(0, min(7, perm))
        item.permission["admin-group"] = 7
        canvas.items[item_id] = item

        # ES 동기화
        es_service.save_canvas(canvas)
        return canvas

    def remove_item(self, canvas_id: int, item_id: str) -> Optional[Canvas]:
        """캔버스에서 특정 아이템을 제거합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas or item_id not in canvas.items:
            return None

        del canvas.items[item_id]
        # ES 동기화
        es_service.save_canvas(canvas)
        return canvas

    def delete_canvas(self, canvas_id: int) -> bool:
        """Elasticsearch에서 캔버스 문서를 삭제합니다."""
        return es_service.delete_canvas(canvas_id)


# 싱글톤 인스턴스
canvas_service = CanvasService()
