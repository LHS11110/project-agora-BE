import logging
from typing import Optional, Dict, Any, List
from elasticsearch import Elasticsearch
from elasticsearch.exceptions import ConnectionError as EsConnectionError, TransportError, NotFoundError

from app.core.config import load_server_config
from app.models.canvas import Canvas

logger = logging.getLogger("elasticsearch_service")


class ElasticsearchService:
    """
    Elasticsearch 클러스터와의 통신 및 캔버스 데이터 색인/조회/삭제 관리 서비스
    """

    def __init__(self):
        self._config = load_server_config().get("elasticsearch", {})
        self.hosts = self._config.get("hosts", ["http://localhost:9200"])
        self.index_name = self._config.get("index", "canvases")
        self.strict_mode = self._config.get("strict_mode", False)

        auth = None
        if self._config.get("username") and self._config.get("password"):
            auth = (self._config["username"], self._config["password"])

        api_key = self._config.get("api_key")

        client_kwargs = {
            "hosts": self.hosts,
            "request_timeout": 3.0,
            "max_retries": 1,
        }
        if auth:
            client_kwargs["basic_auth"] = auth
        if api_key:
            client_kwargs["api_key"] = api_key

        try:
            self.client = Elasticsearch(**client_kwargs)
        except Exception as e:
            logger.warning(f"Elasticsearch 클라이언트 초기화 실패: {e}")
            self.client = None

        self._index_checked = False

    def is_available(self) -> bool:
        """Elasticsearch 서버 응답 가능 여부를 확인합니다."""
        if not self.client:
            return False
        try:
            return bool(self.client.ping())
        except Exception as e:
            logger.debug(f"Elasticsearch ping 실패: {e}")
            return False

    def ensure_index(self) -> bool:
        """지정된 캔버스 인덱스가 존재하는지 확인하고 없으면 자동 생성합니다."""
        if self._index_checked:
            return True
        if not self.client:
            return False

        try:
            if not self.client.indices.exists(index=self.index_name):
                # 캔버스 및 아이템 규격 매핑 정의
                mapping = {
                    "mappings": {
                        "properties": {
                            "canvas-name": {"type": "text", "fields": {"keyword": {"type": "keyword"}}},
                            "canvas-id": {"type": "integer"},
                            "admin": {"type": "integer"},
                            "peoples": {"type": "integer"},
                            "inner-group": {"type": "object"},
                            "items": {"type": "object"},
                            "init-group": {"type": "keyword"}
                        }
                    }
                }
                self.client.indices.create(index=self.index_name, body=mapping)
                logger.info(f"Elasticsearch 인덱스 '{self.index_name}' 생성 완료")
            self._index_checked = True
            return True
        except Exception as e:
            logger.warning(f"Elasticsearch 인덱스 확인/생성 중 오류 발생: {e}")
            if self.strict_mode:
                raise
            return False

    def save_canvas(self, canvas: Canvas) -> bool:
        """
        form.txt 스키마 규격으로 캔버스 데이터를 Elasticsearch에 색인(Index)합니다.
        문서 기본키(ID)는 캔버스 이름(canvas-name)으로 설정합니다.
        """
        if not self.client:
            if self.strict_mode:
                raise RuntimeError("Elasticsearch client is not available")
            return False

        try:
            self.ensure_index()
            # form.txt alias (canvas-name, canvas-id, inner-group 등) 그대로 직렬화
            doc = canvas.model_dump(by_alias=True)
            res = self.client.index(
                index=self.index_name,
                id=canvas.canvas_name,
                document=doc
            )
            logger.info(f"캔버스 '{canvas.canvas_name}'(ID: {canvas.canvas_id}) Elasticsearch 저장 성공 (result: {res.get('result')})")
            return True
        except Exception as e:
            logger.warning(f"캔버스 '{canvas.canvas_name}' Elasticsearch 저장 실패: {e}")
            if self.strict_mode:
                raise
            return False

    def get_canvas_by_name(self, canvas_name: str) -> Optional[Dict[str, Any]]:
        """Elasticsearch 기본키(canvas-name)로 캔버스 문서를 직접 단건 조회합니다."""
        if not self.client:
            return None

        try:
            res = self.client.get(index=self.index_name, id=canvas_name)
            return res.get("_source")
        except NotFoundError:
            return None
        except Exception as e:
            logger.warning(f"Elasticsearch 캔버스 이름 '{canvas_name}' 조회 실패: {e}")
            if self.strict_mode:
                raise
            return None

    def get_canvas_by_id(self, canvas_id: int) -> Optional[Dict[str, Any]]:
        """Elasticsearch에서 canvas-id 필드를 검색하여 캔버스 문서를 조회합니다."""
        if not self.client:
            return None

        try:
            self.ensure_index()
            res = self.client.search(
                index=self.index_name,
                query={"term": {"canvas-id": canvas_id}},
                size=1
            )
            hits = res.get("hits", {}).get("hits", [])
            if hits:
                return hits[0].get("_source")
            return None
        except Exception as e:
            logger.warning(f"Elasticsearch 캔버스 ID {canvas_id} 검색 실패: {e}")
            if self.strict_mode:
                raise
            return None

    def get_canvas(self, canvas_id: int) -> Optional[Dict[str, Any]]:
        """ID를 기반으로 캔버스를 조회합니다."""
        return self.get_canvas_by_id(canvas_id)

    def delete_canvas_by_name(self, canvas_name: str) -> bool:
        """기본키(canvas-name)로 Elasticsearch에서 캔버스 문서를 삭제합니다."""
        if not self.client:
            return False

        try:
            self.client.delete(index=self.index_name, id=canvas_name)
            logger.info(f"캔버스 '{canvas_name}' Elasticsearch 문서 삭제 완료")
            return True
        except NotFoundError:
            return False
        except Exception as e:
            logger.warning(f"Elasticsearch 캔버스 '{canvas_name}' 삭제 실패: {e}")
            if self.strict_mode:
                raise
            return False

    def delete_canvas(self, canvas_id: int) -> bool:
        """canvas-id로 문서를 찾은 후 기본키(canvas-name)를 사용하여 삭제합니다."""
        if not self.client:
            return False

        try:
            doc = self.get_canvas_by_id(canvas_id)
            if doc and "canvas-name" in doc:
                return self.delete_canvas_by_name(doc["canvas-name"])
            return False
        except Exception as e:
            logger.warning(f"Elasticsearch 캔버스 ID {canvas_id} 삭제 실패: {e}")
            if self.strict_mode:
                raise
            return False

    def get_max_canvas_id(self) -> int:
        """Elasticsearch에서 가장 큰 canvas-id 값을 조회합니다."""
        if not self.client:
            return 0
        try:
            self.ensure_index()
            res = self.client.search(
                index=self.index_name,
                query={"match_all": {}},
                size=1,
                sort=[{"canvas-id": {"order": "desc"}}]
            )
            hits = res.get("hits", {}).get("hits", [])
            if hits and "_source" in hits[0]:
                return int(hits[0]["_source"].get("canvas-id", 0))
            return 0
        except Exception:
            return 0

    def list_canvases(self, size: int = 1000) -> List[Dict[str, Any]]:
        """Elasticsearch에서 등록된 캔버스 목록을 조회합니다."""
        if not self.client:
            return []

        try:
            self.ensure_index()
            res = self.client.search(
                index=self.index_name,
                query={"match_all": {}},
                size=size,
                sort=[{"canvas-id": {"order": "asc"}}]
            )
            hits = res.get("hits", {}).get("hits", [])
            return [hit["_source"] for hit in hits if "_source" in hit]
        except Exception as e:
            logger.warning(f"Elasticsearch 캔버스 목록 조회 실패: {e}")
            if self.strict_mode:
                raise
            return []


# 싱글톤 인스턴스
es_service = ElasticsearchService()
