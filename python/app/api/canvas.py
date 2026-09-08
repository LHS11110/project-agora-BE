from typing import List, Dict
from fastapi import APIRouter, HTTPException, status
from pydantic import BaseModel, Field

from app.models.canvas import Canvas, CanvasItem, CreateCanvasRequest
from app.services.canvas_service import canvas_service

router = APIRouter(
    prefix="/api/canvas",
    tags=["Canvas"]
)


class JoinUserRequest(BaseModel):
    uid: int = Field(..., description="참가할 사용자 고유 ID (정수값)")


class AddGroupRequest(BaseModel):
    group_name: str = Field(..., description="추가할 내부 그룹 명칭")


class MoveGroupRequest(BaseModel):
    uid: int = Field(..., description="사용자 UID")
    target_group: str = Field(..., description="이동할 대상 내부 그룹 명칭")


@router.post(
    "",
    response_model=Canvas,
    status_code=status.HTTP_201_CREATED,
    summary="새로운 캔버스 생성"
)
def create_new_canvas(request: CreateCanvasRequest) -> Canvas:
    """
    form.txt 규격에 맞춰 새로운 캔버스를 생성합니다.
    - admin-uid가 필수이며, 자동으로 peoples 및 admin-group에 등록됩니다.
    """
    return canvas_service.create_canvas(request)


@router.get(
    "",
    response_model=List[Canvas],
    summary="전체 캔버스 목록 조회"
)
def list_all_canvases() -> List[Canvas]:
    """현재 서버에 등록된 모든 캔버스를 조회합니다."""
    return canvas_service.list_canvases()


@router.get(
    "/{canvas_id}",
    response_model=Canvas,
    summary="캔버스 상세 조회"
)
def get_canvas_by_id(canvas_id: int) -> Canvas:
    """지정한 ID의 캔버스 전체 데이터(관리자, 참가자, 내부 그룹, 아이템)를 조회합니다."""
    canvas = canvas_service.get_canvas(canvas_id)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.post(
    "/{canvas_id}/join",
    response_model=Canvas,
    summary="사용자 캔버스 참가"
)
def join_canvas(canvas_id: int, req: JoinUserRequest) -> Canvas:
    """캔버스에 사용자를 참가시키고 초기 init-group에 배정합니다."""
    canvas = canvas_service.join_user(canvas_id, req.uid)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.post(
    "/{canvas_id}/groups",
    response_model=Canvas,
    summary="내부 그룹 추가"
)
def add_inner_group(canvas_id: int, req: AddGroupRequest) -> Canvas:
    """캔버스에 새로운 내부 그룹을 추가합니다."""
    canvas = canvas_service.add_inner_group(canvas_id, req.group_name)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.post(
    "/{canvas_id}/move-group",
    response_model=Canvas,
    summary="사용자 내부 그룹 이동"
)
def move_user_group(canvas_id: int, req: MoveGroupRequest) -> Canvas:
    """특정 사용자의 소속 내부 그룹을 변경합니다."""
    canvas = canvas_service.move_user_group(canvas_id, req.uid, req.target_group)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas or User not found")
    return canvas


@router.put(
    "/{canvas_id}/items/{item_id}",
    response_model=Canvas,
    summary="아이템 추가 또는 업데이트"
)
def put_canvas_item(canvas_id: int, item_id: str, item: CanvasItem) -> Canvas:
    """
    form.txt 형식에 맞춰 캔버스에 아이템을 배치합니다.
    - type: 아이템 형식 (정수값)
    - pos: (x, y) 튜플
    - permission: 각 그룹별 권한 (0~7 정수, admin-group은 항상 7로 자동 보장)
    - data1, data2 등 부가 속성 포함 가능
    """
    canvas = canvas_service.put_item(canvas_id, item_id, item)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.delete(
    "/{canvas_id}/items/{item_id}",
    response_model=Canvas,
    summary="아이템 삭제"
)
def delete_canvas_item(canvas_id: int, item_id: str) -> Canvas:
    """캔버스에서 특정 아이템을 삭제합니다."""
    canvas = canvas_service.remove_item(canvas_id, item_id)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas or Item not found")
    return canvas


@router.delete(
    "/{canvas_id}",
    status_code=status.HTTP_204_NO_CONTENT,
    summary="캔버스 삭제"
)
def delete_canvas(canvas_id: int):
    """캔버스를 영구 삭제합니다."""
    success = canvas_service.delete_canvas(canvas_id)
    if not success:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return None
