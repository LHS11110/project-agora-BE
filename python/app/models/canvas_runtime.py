from typing import Dict, List, Optional, Any
from app.services.user_sockets import UserSockets


class Canvas:
    """
    Python 서버에서 관리하는 활성 캔버스 런타임 모델
    
    구조:
    Canvas
        UserSockets (비동기 방식으로 송수신, 소켓 버켓으로 각 사용자마다 송수신 횟수 제한)
        Redis_ip
        Redis_port
    """

    def __init__(
        self,
        canvas_id: int,
        canvas_name: str = "",
        redis_ip: str = "127.0.0.1",
        redis_port: int = 6379,
        admin: Optional[int] = None,
        init_group: str = "default"
    ):
        self.canvas_id: int = canvas_id
        self.canvas_name: str = canvas_name
        self.admin: Optional[int] = admin
        self.peoples: List[int] = [admin] if admin is not None else []
        self.inner_group: Dict[str, List[int]] = {
            "admin-group": [admin] if admin is not None else [],
            init_group: []
        }
        self.items: Dict[str, Any] = {}
        self.init_group: str = init_group

        # 요구사항 필드 정의
        self.Redis_ip: str = redis_ip
        self.Redis_port: int = int(redis_port)
        self.UserSockets: UserSockets = UserSockets(canvas_id=canvas_id)

    # 파이써닉 접근 및 하위 호환성을 위한 프로퍼티 정의
    @property
    def redis_ip(self) -> str:
        return self.Redis_ip

    @redis_ip.setter
    def redis_ip(self, value: str) -> None:
        self.Redis_ip = value

    @property
    def redis_port(self) -> int:
        return self.Redis_port

    @redis_port.setter
    def redis_port(self, value: int) -> None:
        self.Redis_port = int(value)

    @property
    def user_sockets(self) -> UserSockets:
        return self.UserSockets

    def update_redis_config(self, redis_ip: str, redis_port: int) -> None:
        """Redis 접속 정보 갱신"""
        self.Redis_ip = redis_ip
        self.Redis_port = int(redis_port)

    def to_dict(self) -> Dict[str, Any]:
        """JSON 직렬화 가능한 딕셔너리로 변환"""
        return {
            "canvas-id": self.canvas_id,
            "canvas-name": self.canvas_name,
            "admin": self.admin,
            "peoples": list(self.peoples),
            "inner-group": {k: list(v) for k, v in self.inner_group.items()},
            "items": dict(self.items),
            "init-group": self.init_group,
            "redis_ip": self.Redis_ip,
            "redis_port": self.Redis_port,
            "active_connections": self.UserSockets.get_connection_count(),
            "active_users": self.UserSockets.get_active_users()
        }

    def __repr__(self) -> str:
        return (
            f"<Canvas id={self.canvas_id} name='{self.canvas_name}' "
            f"Redis={self.Redis_ip}:{self.Redis_port} "
            f"connections={self.UserSockets.get_connection_count()}>"
        )
