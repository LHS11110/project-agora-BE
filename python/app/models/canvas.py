from typing import Dict, List, Tuple, Any, Optional
from pydantic import BaseModel, Field, ConfigDict, field_validator


class CanvasItem(BaseModel):
    """
    캔버스 위에 배치되는 개별 아이템 모델 (form.txt 규격)
    - type: 아이템의 형식 (정수값)
    - pos: 배치 좌표 (float, float)
    - data1, data2, ... : 가변 속성 (extra='allow')
    - permission: 각 내부 그룹별 아이템 조작 권한 (0~7 정수, admin-group은 항상 7)
    """
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    type: int = Field(..., description="아이템의 형식 (정수값)")
    pos: Tuple[float, float] = Field(..., description="뿌려질 위치 좌표 (float, float)")
    permission: Dict[str, int] = Field(
        default_factory=lambda: {"admin-group": 7},
        description="그룹별 접근 권한 (0~7 범위의 정수, admin-group은 항상 7)"
    )

    @field_validator("permission")
    @classmethod
    def ensure_admin_permission(cls, v: Dict[str, int]) -> Dict[str, int]:
        """권한 값은 항상 0 이상 7 이하의 정수여야 하며, admin-group의 권한은 항상 7로 보장합니다."""
        for group, perm in v.items():
            if not (0 <= perm <= 7):
                raise ValueError(f"권한 값은 0 이상 7 이하의 정수여야 합니다: '{group}'={perm}")
        v["admin-group"] = 7
        return v


class Canvas(BaseModel):
    """
    form.txt 최신 규격을 반영한 캔버스 데이터 모델
    - canvas-name: 생성한 캔버스 이름 (string)
    - canvas-id: 캔버스 아이디 (정수값)
    - admin: 캔버스 관리자 UID (정수값)
    - peoples: 참가한 사람들 명단 [admin-uid, uid1, uid2, ...] (모두 정수값)
    - inner-group: 내부 그룹 및 그룹 명단 {"admin-group": [admin-uid], ...}
    - items: 캔버스에 뿌려질 아이템 맵 (아이템 ID -> CanvasItem)
    - init-group: 참가한 사람의 초기 내부 그룹 명칭
    """
    model_config = ConfigDict(populate_by_name=True, extra="allow")

    canvas_name: str = Field(..., alias="canvas-name", description="생성한 캔버스 이름 (string)")
    canvas_id: int = Field(..., alias="canvas-id", description="캔버스 고유 아이디 (정수값)")
    admin: int = Field(..., description="캔버스 관리자 UID (정수값)")
    peoples: List[int] = Field(default_factory=list, description="참가한 사람들 명단 (admin 포함, 모두 정수값)")
    inner_group: Dict[str, List[int]] = Field(
        default_factory=dict,
        alias="inner-group",
        description="내부 그룹 및 그룹별 소속 UID 목록"
    )
    items: Dict[str, CanvasItem] = Field(
        default_factory=dict,
        description="캔버스에 배치된 아이템 맵 (item_id -> CanvasItem)"
    )
    init_group: str = Field(
        "default",
        alias="init-group",
        description="새로 참가한 사람의 초기 내부 그룹 명칭"
    )


class CreateCanvasRequest(BaseModel):
    """캔버스 생성 요청 DTO"""
    model_config = ConfigDict(populate_by_name=True)

    canvas_name: str = Field(..., alias="canvas-name", description="캔버스 이름")
    admin: int = Field(..., description="캔버스 생성자/관리자 UID (정수값)")
    init_group: Optional[str] = Field("default", alias="init-group", description="초기 내부 그룹명")
