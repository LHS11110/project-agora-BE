import logging
import os
from typing import Optional, List
import redis

logger = logging.getLogger("app.services.redis_service")


class RedisService:
    """Redis 캐시 연동 및 캔버스 관련 키 일괄 삭제 서비스"""

    def __init__(self):
        self.default_password = os.getenv("REDIS_PASSWORD", "AgoraRedisSecret@Passw0rd!2026")

    def _get_client(self, host: str, port: int, password: Optional[str] = None) -> redis.Redis:
        pwd = password if password is not None else self.default_password
        # 비밀번호가 지정된 경우와 없는 경우 모두 대응
        try:
            client = redis.Redis(
                host=host,
                port=port,
                password=pwd,
                socket_timeout=3.0,
                socket_connect_timeout=3.0,
                decode_responses=True
            )
            # ping 테스트
            client.ping()
            return client
        except redis.AuthenticationError:
            # 비밀번호 없이 재시도
            client_no_pwd = redis.Redis(
                host=host,
                port=port,
                socket_timeout=3.0,
                socket_connect_timeout=3.0,
                decode_responses=True
            )
            client_no_pwd.ping()
            return client_no_pwd

    def delete_canvas_cache(self, redis_ip: str, redis_port: int, canvas_id: int) -> bool:
        """
        주어진 Redis 인스턴스에서 해당 캔버스와 관련된 모든 캐시 키를 일괄 제거합니다.
        대상 키 패턴:
        - canvas:{canvas_id}
        - canvas:{canvas_id}:*
        - canvas_cache:{canvas_id}
        - canvas_cache:{canvas_id}:*
        """
        try:
            r = self._get_client(host=redis_ip, port=redis_port)
            patterns = [
                f"canvas:{canvas_id}",
                f"canvas:{canvas_id}:*",
                f"canvas_cache:{canvas_id}",
                f"canvas_cache:{canvas_id}:*",
                f"canvas_{canvas_id}",
                f"canvas_{canvas_id}:*"
            ]

            deleted_total = 0
            for pattern in patterns:
                keys: List[str] = r.keys(pattern)
                if keys:
                    deleted_count = r.delete(*keys)
                    deleted_total += deleted_count
                    logger.info("Deleted %d keys matching pattern '%s' for canvas #%d", deleted_count, pattern, canvas_id)

            logger.info("Successfully cleaned up Redis cache for canvas #%d on %s:%d (Total deleted: %d keys)",
                        canvas_id, redis_ip, redis_port, deleted_total)
            return True
        except Exception as e:
            logger.warning("Failed to delete Redis cache for canvas #%d on %s:%d - %s",
                           canvas_id, redis_ip, redis_port, str(e))
            return False


redis_service = RedisService()
