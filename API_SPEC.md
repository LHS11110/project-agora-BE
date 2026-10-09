# Project Agora API·WebSocket 명세

이 문서는 현재 소스 코드의 Spring Boot API, C++ 제어 API, DB 기반 테스트 페이지 상태 조회와 캔버스 WebSocket 프로토콜을 기준으로 작성되었습니다.

## 공통 규칙

- Spring API 기본 주소: `http(s)://<spring-host>:8080`
- C++ 제어 API 기본 주소: `https://<cpp-host>:8000`
- 모든 JSON 요청과 응답은 `Content-Type: application/json`을 사용합니다.
- 보호된 Spring API는 `Authorization: Bearer <accessToken>`이 필요합니다.
- 로그인 JWT와 캔버스 접속 JWT는 용도가 다릅니다. WebSocket에는 `/access` 응답의 `canvas_access_token`만 사용합니다.
- `/api/**` 요청은 선택적으로 `X-Request-ID`를 받을 수 있습니다. 서버는 안전한 형식(최대 64자)의 값을 유지하고, 누락·잘못된 값이면 새 ID를 생성합니다. 응답에는 `X-Request-ID`가 포함되며, Spring이 C++ REST를 호출할 때도 같은 ID를 전달합니다.
- 캔버스 접근 API의 요청 ID는 서명된 캔버스 접속 토큰에 포함되어 C++ WebSocket 연결 로그까지 이어집니다. WebSocket 메시지도 `request_id`를 포함해야 하며, 클라이언트는 요청별 ID를 생성합니다. 서버가 만든 실패·결과 응답은 가능한 경우 해당 ID를 반환합니다.
- 응답의 사용자 공개 식별자는 `nickname`과 `tag_number` 조합입니다. 내부 `user_id`는 권한·세션 처리에만 사용합니다.

운영 로그는 `ES_LOG_INDEX`에 구조화해 저장됩니다. 공통 필드는 `@timestamp`, `event_id`, `service`, `instance`, `environment`, `version`, `component`, `event`, `level`, `message`이며, 요청 로그에는 `request_id`, `operation`, `outcome`이 추가됩니다. 실패에는 HTTP 상태 또는 `error_code`와 `error_type`이 기록됩니다. 캔버스 WebSocket 메시지와 비동기 Redis 저장은 같은 `request_id`를 사용하고, 접속 요청 ID는 `parent_request_id`로 이어집니다. 경로 로그는 query string을 생략합니다.

### 오류 응답

Spring 오류는 다음 형태입니다.

```json
{
  "code": "CANVAS_001",
  "message": "캔버스를 찾을 수 없습니다.",
  "errors": []
}
```

`errors`는 입력 검증 오류에서만 포함되며 필드별 `field`, `rejectedValue`, `reason`을 담습니다. 주요 상태 코드는 `400`(잘못된 입력), `401`(인증 실패), `403`(권한/비밀번호), `404`(리소스 없음), `409`(중복·동시성 충돌), `503`(일시적으로 사용 가능한 C++ 서버 없음), `500`(서버 오류)입니다. `LB_001`은 활성 C++ 서버를 찾지 못했음을 나타내며 `503`을 반환합니다.

## 1. 인증 API

### 회원가입

`POST /api/auth/signup` 또는 `POST /api/users`

요청:

```json
{"email":"user@example.com","password":"password123","nickname":"사용자"}
```

`email`은 이메일 형식, `password`는 6자 이상, `nickname`은 필수이며 최대 100자입니다. 성공 시 `201`과 [사용자 응답](#사용자-응답)을 반환합니다.

### 로그인

`POST /api/auth/login`

요청은 회원가입과 같은 `email`, `password` 필드를 사용합니다. 성공 응답(`200`):

```json
{"tokenType":"Bearer","accessToken":"<jwt>","user":{ "email":"user@example.com", "nickname":"사용자", "tag_number":1, "role":"ROLE_USER", "status":"ACTIVE" }}
```

### 내 정보 확인

`POST /api/auth/me`

요청 본문에 일반 로그인 JWT를 직접 전달합니다.

```json
{"token":"<login-jwt>"}
```

성공 시 `200`과 사용자 응답, 토큰이 없거나 유효하지 않으면 `401`입니다.

### 사용자 목록·조회·수정·삭제

| 메서드 | 경로 | 인증 | 본문/응답 |
|---|---|---|---|
| `GET` | `/api/auth/users` | 없음 | 사용자 배열 |
| `GET` | `/api/users` | 관리자 | 사용자 배열 |
| `GET` | `/api/users/{nickname}/{tagNumber}` | 필요 | 사용자 1명 |
| `PUT`, `PATCH` | `/api/users/{nickname}/{tagNumber}` | 필요 | `{ "nickname":"새 이름", "password":"새 비밀번호" }` (각 필드 선택) |
| `DELETE` | `/api/users/{nickname}/{tagNumber}` | 필요 | 성공 시 `204` |

사용자 수정·삭제는 서비스의 현재 권한 정책(본인 또는 관리자)을 따릅니다. 탈퇴 대상 사용자가 캔버스를 이용 중(`user_sessions.is_accessed=true`)이면 Spring은 C++ 연결을 강제로 끊지 않고 `409 USER_003`으로 탈퇴를 거부합니다. WebSocket을 종료한 뒤 다시 요청해야 합니다.

### 사용자 응답

```json
{
  "email":"user@example.com",
  "nickname":"사용자",
  "tag_number":1,
  "role":"ROLE_USER",
  "status":"ACTIVE",
  "last_login_at":null,
  "password_chaged_at":null,
  "password_changed_at":null,
  "created_at":"2026-01-01T00:00:00",
  "updated_at":"2026-01-01T00:00:00"
}
```

### 인증 상태

`GET /api/auth/health` → `200`

```json
{"status":"UP","service":"Agora Auth Service"}
```

## 2. 캔버스 API

모든 캔버스 API는 로그인 JWT가 필요합니다.

### 캔버스 생성

`POST /api/canvases`

두 가지 요청 형식을 지원합니다.

- `application/json`: `{ "canvasName":"이름", "description":"설명", "canvasPassword":"비밀번호" }`
- `multipart/form-data`: `canvasName`(필수), `description`, `canvasPassword`, `image`(선택 파일)
- 이미지는 PNG/JPEG만 허용하며 파일은 5MiB 이하, 가로·세로는 각각 2048픽셀 이하여야 합니다. 업로드 파일명·확장자는 저장 형식에 사용하지 않고 PNG로 정규화합니다.

성공 시 `201`:

```json
{"canvas_id":1,"image":"/api/canvases/1/image","description":"설명","canvas_name":"이름","user_count":1}
```

### 목록·검색·단건 조회

| 메서드·경로 | 설명 |
|---|---|
| `GET /api/canvases` | 전체 캔버스. 선택 쿼리 `name` |
| `GET /api/canvases/search?name=이름` | 이름 검색. `name` 생략 가능 |
| `GET /api/canvases/{canvasId}` | 단건 요약 |
| `GET /api/canvases/{canvasId}/image` | 정규화된 PNG 대표 이미지 바이너리 (`image/png`) |

목록 응답은 `CanvasSummaryResponse` 배열입니다.
캔버스 요약의 `image` URL은 대표 이미지 경로를 가리킵니다. 이미지 요청도 Bearer 인증이 필요합니다.

### 캔버스 설정 조회·변경

`GET /api/canvases/{canvasId}/settings` → `200`

```json
{
  "canvas_id":1,
  "canvas_name":"이름",
  "description":"설명",
  "password_protected":true,
  "settings_revision":3,
  "participants":[{"nickname":"사용자","tag_number":1}]
}
```

변경 API는 캔버스가 비활성 상태일 때 관리자 정책으로 사용합니다. 활성 캔버스에 Spring API로 변경 요청을 보내면 `409 CANVAS_006`과 함께 캔버스에 접속해 설정에서 변경하라는 안내를 반환합니다. 활성 WebSocket 세션에서는 아래 WebSocket 설정 이벤트를 사용하며, 성공한 변경은 Redis와 Elasticsearch에 동기 반영됩니다.

| 메서드·경로 | 요청 본문 | 성공 |
|---|---|---|
| `PATCH /api/canvases/{canvasId}/name` | `{ "canvas_name":"새 이름" }` | `204` |
| `PATCH /api/canvases/{canvasId}/description` | `{ "description":"새 설명" }` | `204` |
| `PATCH /api/canvases/{canvasId}/password` | `{ "canvas_password":"새 비밀번호" }` (빈 값은 해제) | `204` |
| `POST /api/canvases/{canvasId}/people` | `{ "nickname":"사용자", "tag_number":1 }` | `204` |
| `DELETE /api/canvases/{canvasId}/people` | 위와 동일 | `204` |
| `DELETE /api/canvases/{canvasId}` | 없음 | `204` |

비밀번호는 저장 시 해시화되며 API 응답에 평문·해시를 포함하지 않습니다. 참여자 관리는 닉네임과 태그 번호로 수행합니다. Spring의 `/access`는 로그인·캔버스 비밀번호를 확인하고 접속 토큰을 발급합니다. 사용자의 SQL 세션이 다른 캔버스에서 활성 상태면 `409 CANVAS_003`을 반환합니다. 같은 캔버스의 추가 WebSocket 연결은 허용합니다. 참여 권한은 C++ WebSocket에서 캐시·세션 예약 전에 다시 확인하며, 로드 후에는 revision만 비교해 그 사이의 설정 변경 경합을 차단합니다.

### 캔버스 접속 정보 발급

`POST /api/canvases/{canvasId}/access`

비밀번호가 있는 캔버스에 처음 접속할 때:

```json
{"password":"캔버스 비밀번호"}
```

비밀번호 확인에 성공하면 서버가 30분짜리 `canvas_password_token`도 발급합니다. 같은 로그인 사용자와 캔버스, 현재 설정 revision에만 사용할 수 있습니다. 다음 접속부터는 비밀번호 대신 해당 토큰을 보냅니다. 비밀번호나 캔버스 설정이 변경되면 기존 토큰은 사용할 수 없습니다.

```json
{"canvas_password_token":"<30분 비밀번호 확인 JWT>"}
```

비밀번호가 없는 캔버스는 본문을 생략하거나 `{}`를 보냅니다. 성공 응답:

```json
{"server_id":5,"ws_port":"8002","canvas_access_token":"<canvas-jwt>","canvas_password_token":"<30분 비밀번호 확인 JWT 또는 null>"}
```

WebSocket에는 `canvas_access_token`만 사용합니다. `canvas_password_token`은 비밀번호 재입력 생략용이며 브라우저 탭의 세션 저장소에만 보관할 수 있습니다. 서버 후보는 DB의 활성화 상태와 최근 15초 heartbeat로 찾고, C++ `GET /api/canvas/count`에서 실시간 활성 캔버스 수를 조회해 P2C로 선택합니다. 서버가 재기동 중이어서 후보를 찾지 못하면 `503 LB_001`을 반환하므로 클라이언트는 짧게 기다렸다가 접속 정보를 다시 요청할 수 있습니다. 활성 세션이 있는 캔버스의 할당 서버 heartbeat가 만료되면 다른 서버로 임의 재할당하지 않고 `409 CANVAS_006`을 반환합니다.

## 3. 로드 밸런서·내부 API

현재 공개된 Spring 경로는 다음과 같습니다. 운영에서는 관리자 또는 내부 네트워크에서만 노출해야 합니다.

| 메서드·경로 | 응답 |
|---|---|
| `GET`, `POST /api/load-balancer/allocate/server` | `{ "ip","port","wsPort","serverIp","serverPort" }` |
| `GET`, `POST /api/load-balancer/allocate/redis` | `{ "ip","port","redisIp","redisPort" }` |
| `GET /api/load-balancer/database` | `{ "ip","port","dbHost","dbPort","dbName","address" }` |
| `POST /api/load-balancer/allocate/database` | 위와 동일 |
| `GET /api/database/address` | 위와 동일(별칭) |

### 테스트 페이지 상태 조회 API

`GET /api/test/cpp-active-canvases`는 테스트베드가 SQL `user_sessions`를 기준으로 활성 캔버스를 확인하는 호환 경로이며 인증된 사용자에게 허용됩니다. C++ HTTP API를 호출하지 않습니다. `GET /api/test/cpp-canvas-count`도 DB 세션 기준 활성 캔버스 수를 반환하며 관리자 전용입니다.

| 메서드·경로 | 응답 |
|---|---|
| `GET /api/test/cpp-active-canvases` | `{ "status":"success", "count":1, "canvases":[{"canvas_id":1,"active_user_count":2}] }` |
| `GET /api/test/cpp-canvas-count` | `{ "status":"success", "count":1 }` |

기존 `/api/test/cpp-access`와 `/api/test/cpp-disconnect` C++ 프록시는 제거했습니다. 일반 접속은 `/api/canvases/{canvasId}/access`를 사용하고, 현재 연결 종료는 클라이언트에서 WebSocket을 닫습니다.

## 4. C++ HTTP 제어 API

C++ REST 포트는 `8000`입니다. Spring은 Wall 내부 HTTPS listener의 `/internal/cpp/servers/{serverId}/api/...`로 호출하고 Nginx가 서버 ID별 주소로 라우팅합니다. 다음 API는 서비스 간 제어용이며 일반 브라우저에 직접 공개하지 않습니다.
`/api/**` 요청에는 Spring이 `.env`의 `CPP_INTERNAL_API_TOKEN`과 일치하는 `X-Agora-Internal-Token` 헤더를 보내야 합니다. `/health`와 `/`는 헬스체크용 공개 경로입니다.

| 메서드·경로 | 응답 |
|---|---|
| `DELETE /api/canvas/{canvasId}` | `{"status":"success","canvas_id":1,"removed":true}` |
| `GET /api/canvas/count` | `{"status":"success","count":1}` |
| `GET /api/canvas/active` | `{"status":"success","count":1,"canvases":[...]}` |
| `GET /health` | `{"status":"UP","service":"Agora C++ Realtime Server"}` |
| `GET /` | `{"status":"online","service":"Agora C++ Server"}` |

`POST /api/access`와 `POST /api/access/disconnect`는 현재 제거되어 더 이상 지원되지 않습니다.

## 5. Phoenix 브로커와 WSS 쿼리

브라우저의 캔버스·RTC 경로는 기존 `/wss/port/{wsPort}/canvas/{canvasId}` 및 `/wss/port/{wsPort}/rtc/canvas/{canvasId}`를 유지합니다. Nginx는 Phoenix로 라우팅합니다. C++의 기존 `/ws/canvas`·`/ws/rtc/canvas` 브라우저 핸들러와 사용자 연결 종료 API는 제거했습니다.

Phoenix → C++는 `wss://<cpp-host>:<wsPort>/broker/queries` 지속 연결에서 `connect`·`query` JSON 프레임을 교환합니다. 각 요청의 `{canvas_id, connection_id, request_id}`가 응답에 그대로 반환되며, 동일 소켓에서 여러 요청이 동시에 진행됩니다. TLS CA·호스트명과 `X-Agora-Internal-Token`을 검증합니다.

사용자 JWT 검증은 Phoenix에서 upgrade 전에 수행합니다. C++는 검증된 principal로 현재 참여자·revision과 최신 문서를 조회하며 사용자 JWT 키나 capability 발급 기능이 없습니다. CRUD·채팅·캔버스 설정은 C++에서 처리하고 결과 및 권한별 방송 정보를 Phoenix가 전달합니다. C++에는 피어 목록·호스트 선출·브라우저 소켓·온라인 사용자 세션 저장이 없습니다.

실제 프레임, 지원 쿼리, 큐 제한, 장애 및 재시도 규칙은 [브로커 프로토콜](../project-agora-Wall/BROKER_PROTOCOL.md)을 참조하세요. SQL `user_sessions` 기반 테스트베드 조회는 브로커의 온라인 상태를 나타내지 않습니다.
