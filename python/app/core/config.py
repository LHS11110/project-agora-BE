import json
import os
from pathlib import Path
from typing import Dict, Any

# python 디렉터리 기준 경로
BASE_DIR = Path(__file__).resolve().parent.parent.parent
CONFIG_PATH = BASE_DIR / "config.json"
TEST_HTML_PATH = BASE_DIR / "test.html"

DEFAULT_SERVER_CONFIG = {
    "public_ip": "127.0.0.1",
    "port": 8000,
    "spring_base_url": "http://127.0.0.1:8080",
    "redis": {
        "host": "127.0.0.1",
        "port": 6379,
        "password": "AgoraRedisSecret@Passw0rd!2026"
    }
}


def load_server_config() -> Dict[str, Any]:
    """config.json 파일에서 서버 IP, 포트 및 Spring / Redis 설정 정보를 읽어옵니다."""
    if not CONFIG_PATH.exists():
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(DEFAULT_SERVER_CONFIG, f, indent=2)
        return DEFAULT_SERVER_CONFIG

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
            return {
                "public_ip": data.get("public_ip", "127.0.0.1"),
                "port": int(data.get("port", 8000)),
                "spring_base_url": data.get("spring_base_url", os.getenv("SPRING_BASE_URL", "http://127.0.0.1:8080")),
                "redis": data.get("redis", DEFAULT_SERVER_CONFIG["redis"])
            }
    except Exception:
        return DEFAULT_SERVER_CONFIG
