from fastapi import APIRouter
from app.models.system import ResourceResponse, NetworkInfoResponse
from app.services.system_service import (
    get_current_resources,
    get_current_network_info
)

router = APIRouter(
    prefix="/api/system",
    tags=["System"]
)


@router.get(
    "/resources",
    response_model=ResourceResponse,
    summary="현재 시스템 리소스 사용량 조회"
)
def read_system_resources() -> ResourceResponse:
    """
    현재 서버의 CPU 사용률, 메모리 상태(전체/사용/가용), 디스크 상태를 실시간 조회합니다.
    """
    return get_current_resources()


@router.get(
    "/network-info",
    response_model=NetworkInfoResponse,
    summary="외부 IP 및 포트 번호 조회 (config.json 기반)"
)
def read_network_info() -> NetworkInfoResponse:
    """
    config.json 파일에 설정된 외부 IP와 포트 번호를 반환합니다.
    """
    return get_current_network_info()
