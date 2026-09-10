import logging
import os
from typing import Dict, List, Optional, Any
import httpx

logger = logging.getLogger("app.services.spring_client")


class SpringClient:
    """
    Spring Boot 백엔드와의 HTTP 통신 클라이언트
    - 정책: Python 서버는 Redis 외 데이터베이스(MSSQL, Elasticsearch)에 직접 접근하지 않음
    - 모든 메타데이터 및 도큐먼트 접근은 Spring API(http://localhost:8080/api/...)를 경유
    """

    def __init__(self, base_url: Optional[str] = None):
        self.base_url = (base_url or os.getenv("SPRING_BASE_URL", "http://127.0.0.1:8080")).rstrip("/")
        self.timeout = 5.0

    def get_canvas(self, canvas_id: int) -> Optional[Dict[str, Any]]:
        """Spring API를 통한 캔버스 메타데이터 단건 조회 (GET /api/canvases/{canvasId})"""
        url = f"{self.base_url}/api/canvases/{canvas_id}"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.get(url)
                if res.status_code == 200:
                    return res.json()
                logger.warning("Spring API get_canvas(%d) returned %d: %s", canvas_id, res.status_code, res.text)
                return None
        except Exception as e:
            logger.warning("Spring API get_canvas(%d) failed: %s", canvas_id, e)
            return None

    def get_canvas_document(self, canvas_id: int) -> Optional[Dict[str, Any]]:
        """Spring API를 통한 캔버스 도큐먼트 조회 (GET /api/canvases/{canvasId}/document)"""
        url = f"{self.base_url}/api/canvases/{canvas_id}/document"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.get(url)
                if res.status_code == 200:
                    return res.json()
                return None
        except Exception as e:
            logger.warning("Spring API get_canvas_document(%d) failed: %s", canvas_id, e)
            return None

    def get_canvas_document_by_name(self, canvas_name: str) -> Optional[Dict[str, Any]]:
        """Spring API를 통한 이름 기준 캔버스 도큐먼트 조회 (GET /api/canvases/name/{canvasName}/document)"""
        url = f"{self.base_url}/api/canvases/name/{canvas_name}/document"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.get(url)
                if res.status_code == 200:
                    return res.json()
                return None
        except Exception as e:
            logger.warning("Spring API get_canvas_document_by_name('%s') failed: %s", canvas_name, e)
            return None

    def list_canvases(self) -> List[Dict[str, Any]]:
        """Spring API를 통한 등록 캔버스 목록 조회 (GET /api/canvases)"""
        url = f"{self.base_url}/api/canvases"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.get(url)
                if res.status_code == 200:
                    return res.json()
                return []
        except Exception as e:
            logger.warning("Spring API list_canvases failed: %s", e)
            return []

    def list_canvas_documents(self) -> List[Dict[str, Any]]:
        """Spring API를 통한 전체 캔버스 도큐먼트 목록 조회 (GET /api/canvases/documents)"""
        url = f"{self.base_url}/api/canvases/documents"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.get(url)
                if res.status_code == 200:
                    return res.json()
                return []
        except Exception as e:
            logger.warning("Spring API list_canvas_documents failed: %s", e)
            return []

    def update_canvas_document(
        self,
        canvas_id: int,
        data: Dict[str, Any],
        token: Optional[str] = None
    ) -> Optional[Dict[str, Any]]:
        """Spring API를 통한 캔버스 도큐먼트 수정 (PUT /api/canvases/{canvasId}/document)"""
        url = f"{self.base_url}/api/canvases/{canvas_id}/document"
        headers = {}
        if token:
            headers["Authorization"] = f"Bearer {token}"
        try:
            with httpx.Client(timeout=self.timeout) as client:
                res = client.put(url, json=data, headers=headers)
                if res.status_code == 200:
                    return res.json()
                return None
        except Exception as e:
            logger.warning("Spring API update_canvas_document(%d) failed: %s", canvas_id, e)
            return None


# 싱글톤 인스턴스
spring_client = SpringClient()
