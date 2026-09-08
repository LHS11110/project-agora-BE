import os
import uvicorn
from fastapi import FastAPI
from fastapi.middleware.cors import CORSMiddleware
from fastapi.responses import FileResponse

from app.api.system import router as system_router
from app.api.canvas import router as canvas_router
from app.core.config import TEST_HTML_PATH, load_server_config

app = FastAPI(
    title="Project Agora Python Service",
    description="FastAPI WebSocket & Resource Monitoring Service",
    version="0.1.0"
)

# 1. CORS 미들웨어 설정
app.add_middleware(
    CORSMiddleware,
    allow_origins=["*"],
    allow_credentials=True,
    allow_methods=["*"],
    allow_headers=["*"],
)

# 2. 라우터 등록
app.include_router(system_router)
app.include_router(canvas_router)


# 3. 루트 헬스체크 및 테스트 대시보드 라우트
@app.get("/", summary="헬스 체크")
def root():
    return {
        "status": "online",
        "service": "project-agora-python",
        "docs_url": "/docs",
        "test_console": "/test"
    }


@app.get("/test", include_in_schema=False)
def serve_test_page():
    """테스트용 HTML 콘솔을 서빙합니다 (test.html 존재 시)."""
    if TEST_HTML_PATH.exists():
        return FileResponse(TEST_HTML_PATH)
    return {"message": "test.html not found"}


if __name__ == "__main__":
    config = load_server_config()
    server_port = int(os.getenv("PORT", config["port"]))
    uvicorn.run("main:app", host="0.0.0.0", port=server_port, reload=True)
