import json
from pathlib import Path
from typing import Dict, Any

# python 디렉터리 기준 경로
BASE_DIR = Path(__file__).resolve().parent.parent.parent
CONFIG_PATH = BASE_DIR / "config.json"
TEST_HTML_PATH = BASE_DIR / "test.html"


DEFAULT_ES_CONFIG = {
    "hosts": ["http://localhost:9200"],
    "index": "canvases",
    "username": None,
    "password": None,
    "api_key": None,
    "strict_mode": False
}


def load_server_config() -> Dict[str, Any]:
    """config.json 파일에서 서버 IP, 포트 및 Elasticsearch 설정 정보를 읽어옵니다."""
    if not CONFIG_PATH.exists():
        default_config = {
            "public_ip": "127.0.0.1",
            "port": 8000,
            "elasticsearch": DEFAULT_ES_CONFIG
        }
        with open(CONFIG_PATH, "w", encoding="utf-8") as f:
            json.dump(default_config, f, indent=2)
        return default_config

    try:
        with open(CONFIG_PATH, "r", encoding="utf-8") as f:
            data = json.load(f)
            es_config = data.get("elasticsearch", {})
            merged_es_config = {**DEFAULT_ES_CONFIG, **es_config}
            return {
                "public_ip": data.get("public_ip", "127.0.0.1"),
                "port": int(data.get("port", 8000)),
                "elasticsearch": merged_es_config
            }
    except Exception:
        return {
            "public_ip": "127.0.0.1",
            "port": 8000,
            "elasticsearch": DEFAULT_ES_CONFIG
        }

