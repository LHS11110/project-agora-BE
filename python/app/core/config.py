import json
from pathlib import Path
from typing import Dict, Any

# python 디렉터리 기준 경로
BASE_DIR = Path(__file__).resolve().parent.parent.parent
CONFIG_PATH = BASE_DIR / "config.json"
TEST_HTML_PATH = BASE_DIR / "test.html"


def load_server_config() -> Dict[str, Any]:
    """config.json 파일에서 서버 IP와 포트 정보를 읽어옵니다."""
    if not CONFIG_PATH.exists():
        default_config = {
            "public_ip": "127.0.0.1",
            "port": 8000
        }
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(default_config, f, indent=2)
        return default_config

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
            return {
                "public_ip": data.get("public_ip", "127.0.0.1"),
                "port": int(data.get("port", 8000))
            }
    except Exception:
        return {
            "public_ip": "127.0.0.1",
            "port": 8000
        }
