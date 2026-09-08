from typing import List
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


class GroupPermissionRequest(BaseModel):
    group_name: str = Field(..., description="그룹 이름")
    permission: int = Field(..., ge=0, le=255, description="1바이트 권한 정수 (0 ~ 255)")


@router.post(
    "",
    response_model=Canvas,
    status_code=status.HTTP_201_CREATED,
    summary="새로운 캔버스 생성"
)
def create_new_canvas(request: CreateCanvasRequest) -> Canvas:
    """form.txt 규격에 맞춰 새로운 캔버스를 생성합니다."""
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
    """지정한 ID의 캔버스 전체 데이터(참가자, 그룹, 아이템)를 조회합니다."""
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
    """캔버스에 사용자를 참가시키고 기본 init-group에 배정합니다."""
    canvas = canvas_service.join_user(canvas_id, req.uid)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.post(
    "/{canvas_id}/permissions",
    response_model=Canvas,
    summary="내부 그룹 권한 설정"
)
def set_permission(canvas_id: int, req: GroupPermissionRequest) -> Canvas:
    """내부 그룹의 1바이트 권한(0~255)을 설정합니다."""
    canvas = canvas_service.set_group_permission(canvas_id, req.group_name, req.permission)
    if not canvas:
        raise HTTPException(status_code=404, detail="Canvas not found")
    return canvas


@router.put(
    "/{canvas_id}/items/{item_id}",
    response_model=Canvas,
    summary="아이템 추가 또는 업데이트"
)
def put_canvas_item(canvas_id: int, item_id: str, item: CanvasItem) -> Canvas:
    """
    form.txt 형식에 맞춰 캔버스에 아이템을 배치합니다.
    - type: 아이템 형식
    - pos: (x, y) 튜플
    - data1, data2 등 부가 속성도 자유롭게 포함 가능
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
