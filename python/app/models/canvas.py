from typing import Dict, List, Tuple, Any, Optional
from pydantic import BaseModel, Field, ConfigDict


class CanvasItem(BaseModel):
    """
    캔버스 위에 배치되는 개별 아이템 모델 (form.txt 규격)
    - type: 아이템의 형식 (정수값)
    - pos: 배치 좌표 (float, float)
    - data1, data2, ... : 아이템 타입마다 달라지는 가변 속성들을 자유롭게 포함할 수 있도록 extra='allow' 적용
    """
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    type: int = Field(..., description="아이템의 형식 (정수값)")
    pos: Tuple[float, float] = Field(..., description="뿌려질 위치 좌표 (float, float)")


class Canvas(BaseModel):
    """
    form.txt 규격을 완벽히 준수하는 캔버스 데이터 모델
    - peoples: 참가한 사람들 UID 명단 (모두 정수값)
    - inner-group: 내부 그룹 및 그룹 명단 (UID 정수값 리스트)
    - inner-group-permission: 각 내부 그룹의 권한 (1바이트 정수: 0 ~ 255)
    - items: 캔버스에 뿌려질 아이템들 (아이템 식별자 키 기반 Dict[str, CanvasItem])
    """
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    canvas_name: str = Field(..., alias="canvas-name", description="생성한 캔버스 이름 (string)")
    canvas_id: int = Field(..., alias="canvas-id", description="캔버스 아이디 (정수값)")
    peoples: List[int] = Field(default_factory=list, description="참가한 사람들 명단 (UID 정수값)")
    inner_group: Dict[str, List[int]] = Field(
        default_factory=dict,
        alias="inner-group",
        description="내부 그룹 및 그룹 명단 (소속 UID 정수값 리스트)"
    )
    inner_group_permission: Dict[str, int] = Field(
        default_factory=dict,
        alias="inner-group-permission",
        description="각 내부 그룹의 권한 (1바이트 정수: 0 ~ 255)"
    )
    items: Dict[str, CanvasItem] = Field(
        default_factory=dict,
        description="캔버스에 뿌려질 아이템 맵 (아이템 ID -> CanvasItem)"
    )
    init_group: str = Field(
        "default",
        alias="init-group",
        description="참가한 사람의 초기 내부 그룹 명칭"
    )


class CreateCanvasRequest(BaseModel):
    """캔버스 생성 요청 DTO"""
    model_config = ConfigDict(populate_by_name=True)

    canvas_name: str = Field(..., alias="canvas-name", description="캔버스 이름")
    init_group: Optional[str] = Field("default", alias="init-group", description="초기 내부 그룹명")
    initial_permission: Optional[int] = Field(1, ge=0, le=255, description="초기 그룹 권한 (1바이트 정수: 0~255)")
