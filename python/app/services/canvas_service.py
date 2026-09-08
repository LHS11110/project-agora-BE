from typing import Dict, List, Optional
from app.models.canvas import Canvas, CanvasItem, CreateCanvasRequest


class CanvasService:
    """캔버스 생성, 조회, 수정 및 관리 비즈니스 로직을 수행하는 서비스"""

    def __init__(self):
        self._canvases: Dict[int, Canvas] = {}
        self._next_id: int = 1

    def create_canvas(self, req: CreateCanvasRequest) -> Canvas:
        """새로운 캔버스를 생성하고 초기 그룹 및 권한을 세팅합니다."""
        canvas_id = self._next_id
        self._next_id += 1

        init_group = req.init_group or "default"
        initial_perm = max(0, min(255, req.initial_permission or 1))

        canvas = Canvas(
            **{
                "canvas-name": req.canvas_name,
                "canvas-id": canvas_id,
                "peoples": [],
                "inner-group": {init_group: []},
                "inner-group-permission": {init_group: initial_perm},
                "items": {},
                "init-group": init_group
            }
        )

        self._canvases[canvas_id] = canvas
        return canvas

    def get_canvas(self, canvas_id: int) -> Optional[Canvas]:
        """ID로 캔버스를 단건 조회합니다."""
        return self._canvases.get(canvas_id)

    def list_canvases(self) -> List[Canvas]:
        """등록된 전체 캔버스 목록을 반환합니다."""
        return list(self._canvases.values())

    def join_user(self, canvas_id: int, uid: int) -> Optional[Canvas]:
        """캔버스에 사용자를 참가시키고 초기 내부 그룹에 소속시킵니다."""
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

        return canvas

    def set_group_permission(self, canvas_id: int, group_name: str, permission: int) -> Optional[Canvas]:
        """특정 내부 그룹의 권한(1바이트 정수: 0~255)을 설정합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        canvas.inner_group_permission[group_name] = max(0, min(255, permission))
        if group_name not in canvas.inner_group:
            canvas.inner_group[group_name] = []

        return canvas

    def put_item(self, canvas_id: int, item_id: str, item: CanvasItem) -> Optional[Canvas]:
        """캔버스에 아이템을 배치하거나 업데이트합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas:
            return None

        canvas.items[item_id] = item
        return canvas

    def remove_item(self, canvas_id: int, item_id: str) -> Optional[Canvas]:
        """캔버스에서 특정 아이템을 제거합니다."""
        canvas = self.get_canvas(canvas_id)
        if not canvas or item_id not in canvas.items:
            return None

        del canvas.items[item_id]
        return canvas

    def delete_canvas(self, canvas_id: int) -> bool:
        """캔버스를 삭제합니다."""
        if canvas_id in self._canvases:
            del self._canvases[canvas_id]
            return True
        return False


# 싱글톤 인스턴스
canvas_service = CanvasService()
