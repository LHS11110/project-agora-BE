# Agora API 벤치마크 결과

HTTP 서버 코드에 계측을 추가하지 않고 외부 Python 클라이언트로 측정했습니다. 각 응답 본문을 모두 읽은 시점까지의 순차 요청 왕복시간입니다.

## 실행 조건

- 실행 시각: 2026-10-05 08:38 UTC, 총 27.4분
- HTTP: 41 메서드/경로·콘텐츠 유형 조합, 43,000회 성공 응답
- 기본 표본은 경로당 1,000회입니다. 생성 경로를 성공 사례로 측정하기 위해 임시 사용자 2,000개와 캔버스 2,000개를 만들었고, 해당 삭제 경로는 각 2,000회 측정했습니다.
- 요청 크기는 본문 바이트와 URL query 바이트의 합이며 HTTP 헤더·TCP 프레임은 제외했습니다. 응답 크기는 실제로 읽은 본문 바이트입니다.
- Spring은 `127.0.0.1:8080`, C++ REST는 `127.0.0.1:8000`의 로컬 Docker 포트로 측정했습니다. TLS, Nginx, 외부망 지연은 포함하지 않았습니다.
- HTTP 연결은 재사용했고 동시 부하는 걸지 않았습니다.
- 정리 확인: 임시 사용자 행 0개, 임시 캔버스 잔여 0개. 기존 canvas_info 행은 1개로 유지됐습니다.

## 서버별 요약

| 서버 | 메서드/경로 수 | 요청 수 | 가중 평균 | 평균 요청 데이터 | 평균 응답 본문 | 성공 응답 |
|---|---:|---:|---:|---:|---:|---:|
| Spring Boot | 34 | 36,000 | 40.16 ms | 188.6 B | 254.2 B | 36,000 |
| C++ REST | 7 | 7,000 | 27.20 ms | 0.0 B | 49.4 B | 7,000 |

## 관찰된 경로

| 기능 | 본문/쿼리 크기 | 평균 왕복 | p95 | 응답 본문 |
|---|---:|---:|---:|---:|
| C++ GET /api/canvas/active (fixed) | 0 B | 27.21 ms | 43.91 ms | 44 B |
| C++ GET /api/canvas/count (fixed) | 0 B | 27.19 ms | 43.85 ms | 30 B |
| C++ GET /health (fixed) | 0 B | 27.14 ms | 43.92 ms | 53 B |
| Spring DELETE /api/canvases/{canvasId} (fixed) | 0 B | 57.91 ms | 67.26 ms | 0 B |
| Spring GET /api/canvases/search (query=0) | 0 B | 13.31 ms | 18.69 ms | 231 B |
| Spring GET /api/canvases/search (query=2048) | 2053 B | 11.50 ms | 18.85 ms | 2 B |
| Spring GET /api/canvases/search (query=512) | 517 B | 9.23 ms | 12.93 ms | 2 B |
| Spring GET /api/canvases/search (query=64) | 69 B | 8.58 ms | 11.64 ms | 2 B |
| Spring GET /api/canvases/{canvasId}/settings (fixed) | 0 B | 8.78 ms | 12.60 ms | 192 B |
| Spring PATCH /api/canvases/{canvasId}/description (description=0) | 18 B | 37.10 ms | 50.84 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/description (description=1024) | 1042 B | 36.84 ms | 49.72 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/description (description=256) | 274 B | 36.76 ms | 50.00 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/description (description=4000) | 4018 B | 37.27 ms | 51.48 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/password (password=24) | 46 B | 194.64 ms | 223.68 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/password (password=48) | 70 B | 196.12 ms | 224.35 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/password (password=64) | 86 B | 197.28 ms | 222.67 ms | 0 B |
| Spring PATCH /api/canvases/{canvasId}/password (password=8) | 30 B | 195.30 ms | 222.09 ms | 0 B |
| Spring POST /api/auth/login (fixed) | 73 B | 189.56 ms | 214.44 ms | 583 B |
| Spring POST /api/auth/signup (fixed) | 136 B | 176.81 ms | 212.78 ms | 325 B |
| Spring POST /api/canvases [JSON] (description=0) | 62 B | 38.94 ms | 50.30 ms | 128 B |
| Spring POST /api/canvases [JSON] (description=1024) | 1086 B | 39.23 ms | 52.89 ms | 1152 B |
| Spring POST /api/canvases [JSON] (description=256) | 318 B | 38.63 ms | 52.37 ms | 384 B |
| Spring POST /api/canvases [JSON] (description=4000) | 4062 B | 38.89 ms | 50.40 ms | 4128 B |
| Spring POST /api/canvases [multipart] (description=0) | 319 B | 38.98 ms | 48.12 ms | 130 B |
| Spring POST /api/canvases [multipart] (description=1024) | 1343 B | 39.11 ms | 48.87 ms | 1154 B |
| Spring POST /api/canvases [multipart] (description=256) | 575 B | 38.93 ms | 48.27 ms | 386 B |
| Spring POST /api/canvases [multipart] (description=4000) | 4319 B | 39.17 ms | 48.25 ms | 4130 B |
| Spring POST /api/canvases/{canvasId}/access (fixed) | 79 B | 148.66 ms | 192.12 ms | 759 B |
| Spring POST /api/users (fixed) | 137 B | 161.74 ms | 212.09 ms | 326 B |

## 요청 크기별 결과

- 캔버스 설명 PATCH의 본문은 약 18 B에서 4,018 B까지 바꿨습니다. 평균은 36.76–37.27 ms, p95는 약 50–51 ms로 크기에 따른 차이가 거의 없었습니다.
- JSON 캔버스 생성 본문은 62–4,062 B, multipart 생성은 319–4,319 B였고 두 형식 모두 평균 약 39 ms였습니다.
- 생성 응답은 캔버스 설명을 그대로 포함해 0 B 설명에서 약 128–130 B, 4,000자 설명에서 약 4.1 KB로 커졌습니다. 생성 응답 크기는 고정이 아닙니다.
- 검색 query는 0–2,053 B 범위였습니다. query가 없는 검색은 기존 캔버스 목록을 반환했고, 검색어가 있는 경우 결과가 없어 약 2 B 응답을 반환했습니다.
- 가장 느린 경로는 BCrypt를 수행하는 캔버스 비밀번호 변경(평균 195.83 ms), 로그인(189.56 ms), 회원가입(176.81 ms)이었습니다. 이 차이는 요청 크기보다 암호 해시 검증/생성 비용이 큽니다.

요청 크기별 전체 평균·p50·p95·응답 바이트는 CSV에 있고, JSON에는 같은 측정값과 상태 코드 분포가 있습니다.

## WebSocket 제한

WebSocket 경로는 같은 Docker 네트워크의 별도 클라이언트에서 재시도했습니다. 두 경로 모두 HTTP `200`과 `Upgrade: websocket` 헤더를 받았지만 정상 업그레이드에 필요한 `101 Switching Protocols`와 초기 프레임은 받지 못했습니다. 각 경로는 3회 시도 후 중단되어 성공 핸드셰이크 평균은 산출하지 않았습니다. 상세값은 별도 JSON에 있습니다.

C++ disconnect/delete 제어 경로는 실제 접속자나 캔버스를 끊지 않도록 존재하지 않는 양수 ID로 측정했습니다. 따라서 해당 수치는 no-op 경로 응답시간이며 활성 세션 정리 작업 시간은 아닙니다.

## 파일

- `agora-api-benchmark-2026-10-05.csv`: HTTP 경로와 요청 크기 그룹별 원자료 요약
- `agora-api-benchmark-2026-10-05.json`: HTTP 측정 메타데이터와 상태별 결과
- `agora-ws-benchmark-2026-10-05.json`: WebSocket 핸드셰이크 시도 결과
