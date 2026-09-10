from typing import Optional, Dict, Any
from fastapi import APIRouter, Query, status

from app.services.canvas_service import canvas_service

router = APIRouter(
    prefix="/api/canvas",
    tags=["Canvas"]
)


@router.delete(
    "/{canvas_id}",
    status_code=status.HTTP_200_OK,
    summary="서버 메모리 및 Redis 캐시에서 캔버스 일괄 제거"
)
def delete_canvas_cache(
    canvas_id: int,
    redis_ip: Optional[str] = Query(None, alias="redisIp", description="할당된 Redis IP"),
    redis_port: Optional[str] = Query(None, alias="redisPort", description="할당된 Redis Port")
) -> Dict[str, Any]:
    """
    Spring 백엔드로부터 캔버스 삭제 요청을 수신하여:
    1. Python 서버 자체 메모리/세션에서 해당 캔버스 정리
    2. 할당된 Redis 인스턴스에서 해당 캔버스의 모든 캐시 키(canvas:{id}*) 일괄 삭제
    를 수행합니다.
    """
    result = canvas_service.delete_canvas_from_server_and_redis(
        canvas_id=canvas_id,
        redis_ip=redis_ip,
        redis_port=redis_port
    )
    return result
