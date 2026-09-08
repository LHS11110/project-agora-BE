from typing import Dict, List, Tuple, Any, Optional
from pydantic import BaseModel, Field, ConfigDict


class CanvasItem(BaseModel):
    """캔버스 위에 배치되는 개별 아이템 모델"""
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    type: Any = Field(..., description="아이템 형식 (item_id 또는 타입 명칭)")
    pos: Tuple[float, float] = Field(..., description="아이템의 2차원 좌표 (x, y)")


class Canvas(BaseModel):
    """form.txt에 정의된 캔버스 데이터 모델"""
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    canvas_name: str = Field(..., alias="canvas-name", description="생성한 캔버스 이름")
    canvas_id: int = Field(..., alias="canvas-id", description="캔버스 고유 아이디 (정수값)")
    peoples: List[str] = Field(default_factory=list, description="참가한 사용자 UID 목록")
    inner_group: Dict[str, List[str]] = Field(
        default_factory=dict,
        alias="inner-group",
        description="내부 그룹 및 각 그룹별 소속 사용자 UID 목록"
    )
    inner_group_permission: Dict[str, int] = Field(
        default_factory=dict,
        alias="inner-group-permission",
        description="각 내부 그룹의 권한 (1바이트 정수: 0 ~ 255)"
    )
    items: Dict[str, CanvasItem] = Field(
        default_factory=dict,
        description="캔버스에 배치된 아이템 목록 (아이템 ID를 Key로 관리)"
    )
    init_group: str = Field(
        "default",
        alias="init-group",
        description="새로 참가한 사용자에게 기본 부여되는 초기 내부 그룹 이름"
    )


class CreateCanvasRequest(BaseModel):
    """캔버스 생성 요청 DTO"""
    model_config = ConfigDict(populate_by_name=True)

    canvas_name: str = Field(..., alias="canvas-name", description="캔버스 이름")
    init_group: Optional[str] = Field("default", alias="init-group", description="초기 그룹명")
    initial_permission: Optional[int] = Field(1, description="초기 그룹 권한 (1바이트 정수, 기본 1)")
