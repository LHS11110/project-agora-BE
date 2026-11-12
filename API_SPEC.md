# Project Agora API·WebSocket 명세

## VS Code VM workspace files

The authenticated canvas WebSocket accepts `workspace_files` requests. Paths are canonical absolute paths relative to the assigned VM's `/workspace`; no host path or caller-selected VM ID is accepted. C++ derives the canvas and receipt owner from the verified connection identity and checks the current settings revision. Canvas read access grants access to the entire workspace directory; mutations require canvas write access. Canvas object ACLs are not file ACLs.

```json
{"type":"workspace_files","request_id":"stable-mutation-uuid","operation":"read","path":"/src/main.lean"}
```

Operations: `stat`, `list` (optional integer `cursor`), `read`, `write`, `mkdir`, `rename` (`target`), `delete` (`recursive`). A successful response is `{type:"workspace_files_result", request_id, ok:true, result:{...}}`; failure has `ok:false, code`. Reads return file metadata, `fileId`, `etag`, base64 `content` and an Automerge `document` (null for binary files). Text writes send `fileId` and `document`. Binary writes send `fileId`, `etag` and `content`. New files send `create:true`, `overwrite:false` and `content`. Rename/delete send the current stat `resourceId`; rename refuses existing destinations.

The guest merges text documents before atomically replacing the actual file. Mutations carry durable receipts scoped by server-owned user ID and request ID. Retrying uses the exact same ID and payload. Wall broadcasts `{type:"workspace_files_changed", operation, path, target?, created?, request_id}` only after success; broadcasts carry no file contents. Clients read again on reconnect and invalidation, merge pending local edits, and periodically refresh opened files to observe external programs.

File contents and serialized Automerge documents are each limited to 128 KiB; requests are at most 700,000 bytes, responses below 400,000 bytes. Lists return 256 entries per page and reject directories exceeding 10,000 supported entries. Symlinks, special files and traversal paths are rejected. Binary stale writes return `FILE_CONFLICT`, old file generations return `FILE_REPLACED`, and old active VM agents return `WORKSPACE_AGENT_UPGRADE_REQUIRED` without restarting the VM.

The matching web extension is `IRADE/agora-workspace`. Web worker origins may be explicitly allowed using `CORS_ALLOWED_ORIGIN_PATTERNS`, for example `https://*.ide.example.com:8080`. Existing exact `CORS_ALLOWED_ORIGINS` are preserved; origin patterns default to empty.

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

`VM_001`은 Spring이 호스트의 가상머신 매니저 소켓에 연결하지 못했음을, `VM_002`는 매니저는 연결됐으나 이 캔버스의 VM 시작·실행이 실패했음을 나타내며 모두 `503`을 반환합니다. 이는 C++ 실시간 서버 할당 오류 `LB_001`과 구분됩니다.

## C++ 서버 등록 관리

서버 등록 목록은 SQL `cpp_server` 테이블을 기준으로 합니다. C++ 서버는 시작 시 자기 주소와 REST/WebSocket 포트를 등록하고 5초마다 heartbeat를 갱신합니다. 목록과 등록 관리 API는 관리자만 호출할 수 있습니다.

| 메서드 | 경로 | 본문/응답 |
|---|---|---|
| `GET` | `/api/servers` | 서버 배열. 각 항목에 `serverId`, `serverIp`, `serverPort`, `wsPort`, `isActive`, `currentLoad` 포함 |
| `GET` | `/api/servers/{serverId}` | 등록된 서버 한 개 |
| `POST` | `/api/servers` | `{ "serverIp":"agora-cpp", "serverPort":"8000", "wsPort":"8002" }`로 서버 등록·활성화 |
| `PUT` | `/api/servers/{serverId}` | 서버 주소·포트 변경 또는 비활성 서버 재활성화 |
| `DELETE` | `/api/servers/{serverId}` | 미사용 서버 삭제. 사용 중이면 삭제 대신 비활성화 |

캔버스 접속용 서버 선택은 최근 15초 내 heartbeat가 있고 내부 C++ 상태 확인에 성공한 서버만 사용합니다. DB에 등록된 행이 있어도 heartbeat가 오래됐거나 상태 확인이 실패하면 `503 LB_001`이 반환됩니다.

## 1. 인증 API

### 회원가입

`POST /api/auth/signup` 또는 `POST /api/users`

요청:

```json
{"email":"user@example.com","password":"Example-pass12!","nickname":"사용자","emailVerificationToken":"인증 성공 시 받은 일회용 토큰"}
```

`email`은 이메일 형식, `password`는 12자 이상이며 문자·숫자·특수문자를 포함해야 합니다(공백 불가, UTF-8 기준 최대 72바이트). 비밀번호 변경에도 같은 기준을 적용하며 기존 비밀번호로 로그인할 때는 새 기준을 소급 적용하지 않습니다. `nickname`은 필수이며 최대 100자입니다. 성공 시 `201`과 [사용자 응답](#사용자-응답)을 반환합니다.

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

비밀번호 확인에 성공하면 서버가 4시간짜리 `canvas_password_token`도 발급합니다. 같은 로그인 사용자와 캔버스, 현재 설정 revision에만 사용할 수 있습니다. 다음 접속부터는 비밀번호 대신 해당 토큰을 보냅니다. 토큰 재사용은 유효 기간을 연장하지 않습니다. 이전 세대의 30분 토큰은 거부하며, 비밀번호나 캔버스 설정이 변경된 뒤에는 기존 토큰을 사용할 수 없습니다.

```json
{"canvas_password_token":"<4시간 비밀번호 확인 JWT>"}
```

비밀번호가 없는 캔버스는 본문을 생략하거나 `{}`를 보냅니다. 성공 응답:

```json
{"server_id":5,"ws_port":"8002","canvas_access_token":"<canvas-jwt>","canvas_password_token":"<4시간 비밀번호 확인 JWT 또는 null>"}
```

WebSocket에는 `canvas_access_token`만 사용합니다. `canvas_password_token`은 비밀번호 재입력 생략용으로 브라우저 저장소에 보관되며, 최초 비밀번호 확인 시점부터 4시간 뒤 만료됩니다. 재사용이나 재접속으로 만료 시각이 연장되지 않습니다. 보호된 캔버스의 WebSocket 토큰도 이 인증의 남은 시간보다 오래 유효하지 않아, 만료되면 연결을 다시 인증해야 합니다. 서버 후보는 DB의 활성화 상태와 최근 15초 heartbeat로 찾고, C++ `GET /api/canvas/count`에서 실시간 활성 캔버스 수를 조회해 P2C로 선택합니다. 서버가 재기동 중이어서 후보를 찾지 못하면 `503 LB_001`을 반환하므로 클라이언트는 짧게 기다렸다가 접속 정보를 다시 요청할 수 있습니다. 활성 세션이 있는 캔버스의 할당 서버 heartbeat가 만료되면 다른 서버로 임의 재할당하지 않고 `409 CANVAS_006`을 반환합니다.

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

### 캔버스 그룹별 읽기·쓰기·실행 권한

`GET /api/canvases/{id}/settings` 응답의 `group_permissions`에는 그룹별 `{read, write, execute}` boolean, `can_manage`에는 호출자의 관리 가능 여부가 포함됩니다. 활성 캔버스의 관리자는 WSS `canvas_settings_update` 이벤트에 `field: "group_permissions"`, 권한 맵 `value`, 현재 `expected_revision`을 보냅니다. 관리자 그룹을 제한하거나 읽기 없이 쓰기/실행을 부여하는 요청은 거부합니다. 권한 변경은 기존 연결에도 반영됩니다.

읽기는 조회·접속, 쓰기는 객체 변경·채팅/댓글, 실행은 코드 실행을 허용합니다. 결과 객체의 결과 데이터 갱신에는 실행 권한을 적용하며, 실행 환경 설정·리소스 변경·초기화에는 쓰기와 실행이 모두 필요합니다. 캔버스 UI의 코드 객체·노트북 셀 실행은 이미 인증된 캔버스 WebSocket에서 브로커로 전송하며 Spring 실행 API를 거치지 않습니다. Phoenix 브로커는 실행 그룹 권한을 확인해 요청을 비동기 전달하고, C++ 브로커는 최신 캔버스 revision과 대상 객체 ACL을 다시 확인한 뒤 VM 관리자 Unix 소켓으로 전달합니다. 결과는 같은 WebSocket 연결의 `runtime_result` 이벤트로 요청자에게 돌아옵니다. 실행 중에도 캔버스 편집·채팅 질의는 계속 처리됩니다.

### Runtime standard input

The canvas UI sends `{ type: "runtime_execute", action: "execute"|"build", object_id, language, code, stdin?, request_id }` over its authenticated canvas WebSocket. Notebook cells use `action: "notebook"` with `object_id` (cell object or legacy notebook root), `notebook_id`, `cell_id`, and `code`. Phoenix derives the user identity from the connected canvas session, checks canvas-level execute permission, and sends the request asynchronously to C++. C++ checks the current settings revision, execute-group intersection, and code/notebook cell ACL, then forwards a bounded request over the VM manager Unix socket. For normal code execution, stdout/stderr chunks return as correlated `runtime_output` frames while the worker is running; the final worker result or error returns as `runtime_result` on the same WebSocket. Notebook rich outputs and build results return in their final `runtime_result`. Code and stdin are each limited to 65,536 UTF-8 bytes. The REST endpoints remain for SDK/automation compatibility; the canvas UI does not use them to execute code. Missing stdin retains EOF behavior; an explicitly empty string represents connected but empty input. Input resets for each code step without resetting the language environment. The frontend builds stdin from output-to-code connections in display order, appending a newline boundary between nonempty output records. Result-to-code edges participate in DAG ordering and cycle rejection.

### Canvas-scoped bot participants / Python SDK

The Python package is in `sdk/python` (`pip install ./sdk/python`). It uses existing HTTP APIs and broker protocol 2 (`canvas_ops`, durable receipts, `canvas_sync`, settings and events). No database credentials or internal ports are needed.

`GET /api/auth/me` returns the authenticated principal (the existing POST token check remains supported). An authenticated canvas owner/admin-group user can `POST /api/canvases/{id}/bots` with `{name}` to create a scoped service account. The response `{bot:{user_id,nickname,tag_number,canvas_id,active},token}` discloses the random 256-bit credential once. The SDK then invites the account via existing `participant_add`; an uninvited bot cannot access even a public canvas. Direct REST consumers must perform that invitation themselves. Accounts use `oauth_provider=agora-bot`, a canvas/creator identifier in `oauth_id`, and only a SHA-256 token hash in `password_hash`; there is no schema change or local password login for these accounts.

`GET /api/canvases/{id}/bots` lists identities without secrets. `POST .../bots/{user_id}/rotate` replaces a live credential and returns the new token once; `DELETE .../bots/{user_id}` revokes it. Both require a real manager, not another bot. Use `Authorization: Bearer agora_bot_<id>.<secret>` for the bot; every HTTP call validates active status, credential hash, creator active status, explicit membership, current read permission and a route/canvas allowlist. Bots cannot access account/security/other-canvas/management APIs. Existing write/execute checks apply in C++ and the runtime API. Bot WSS leases last at most 60 seconds and reconnect through the normal access API. HTTP-only revocation takes effect immediately on new requests and at lease expiry on existing WSS. The SDK `CanvasSession.revoke_bot` first removes membership to terminate existing sockets immediately through the existing settings revision broadcast.

Text/note/code objects may now be created with canonical string content and no Automerge metadata. A whole-content replacement uses the existing `patch` operation, sets the appropriate `text`/`code` field and unsets **both** `automerge_snapshot` and `automerge_changes`. Partial/malformed CRDT metadata is still rejected. Existing browser CRDT requests continue to union changes; plain replacements intentionally reset the document and are not character-wise merges. ACL and object locks are unchanged. This is used by the Python SDK and permits later browser editing through its existing canonical-text fallback.

### Online group management

Canvas settings responses now include `groups: {name: [{nickname,tag_number}]}`, `initial_group`, and `owner: {nickname,tag_number}`. Canvas admins can use the existing settings WebSocket CAS protocol with fields `group_create` (`value: {name,permissions?}`), `group_members` (`{name,members:[handle]}`), `group_initial` (`{name}`), `group_rename` (`{name,new_name}`), and `group_delete` (`{name}`). New groups default to read-only. Members must already be registered participants. Administrator-group identity and owner membership are protected; initial or object-referenced groups cannot be deleted. Names are at most 80 UTF-8 bytes, without dots/control/prototype-reserved names. Renaming migrates object ACLs and persisted items-b64 without changing object locks. A settings revision broadcast reconnects all clients for fresh capabilities.

`participant_add` optionally accepts `group` beside nickname/tag_number to enroll a newly created bot or ordinary participant directly into a readable non-admin group. Omitted group uses the configured initial group. Bot creation UI is available to managers in canvas settings; it uses the same scoped credential APIs and existing enrollment/revocation routes, with one-time copy/download of the credential.

### 참여자 역할 지정과 새 객체 공유 범위

`canvas_settings_update`의 `field: "participant_groups"`, `value: {"nickname":"...","tag_number":1,"groups":["design","research"]}`로 참여자 한 명의 역할 집합을 원자적으로 교체합니다. `request_id`, `expected_revision`이 필요하며 캔버스 관리자만 호출할 수 있습니다. 등록되지 않은 참여자·존재하지 않는 그룹은 거부하고, 소유자의 `admin-group`은 제거할 수 없습니다.

초기 브로커 응답/인증된 identity의 `member_groups`는 모든 소속, `groups`는 읽기 가능한 소속, `write_groups`·`execute_groups`는 해당 기능이 허용되는 소속입니다. 새 객체의 기본 `permission`은 `member_groups` 배열이며, 미지정 create 요청에는 서버가 기본값을 채웁니다. 직접 지정하는 경우 일반 사용자는 자신의 소속 그룹만 포함할 수 있습니다. 읽기는 ACL과 읽기 가능 그룹의 교집합이 있을 때 허용하고, 쓰기·실행 권한은 별도로 검사합니다. 관리자는 모든 객체를 볼 수 있습니다. Python SDK 0.1.1도 동일한 기본값을 사용합니다.


### 그룹 비트 레이아웃과 3비트 권한

관리자·기본 그룹을 포함해 최대 64개입니다. 초과 생성은 `GROUP_LIMIT`으로 거부됩니다. 초기 브로커 응답/인증 identity의 `group_bits`는 `{그룹이름: 슬롯번호(0~63)}`, `group_masks`는 `{member,read,write,execute}`에 해당하는 16자리 16진 문자열입니다. 최상위 비트는 `8000000000000000`이며 Number로 변환하지 말고 JS BigInt 또는 Python `int(mask,16)`으로 해석합니다.

저장 필드 `group-permissions`는 읽기=1, 쓰기=2, 실행=4의 3비트 조합입니다. `group_create.permissions`, `group_permissions` 변경에서 기존 불리언 맵과 정수 조합을 모두 받습니다. 읽기 없는 쓰기/실행 조합은 거부하고 관리자 그룹은 7을 유지합니다. UI 호환용 설정 응답 `group_permissions`는 불리언 맵이며 `group_permission_bits`로 정수 맵도 제공합니다. `group-bits`의 슬롯은 이름을 변경해도 유지됩니다. 사용자가 보내는 임의의 비트 마스크로 접근 권한을 부여하지 않습니다.

### 사전 빌드 실행

Legacy API clients can use `POST /api/canvases/{canvasId}/runtime/build` with `language`, `code`, and `passwordGrant`. The canvas UI sends build requests through `runtime_execute` on its current WebSocket. The request checks execute access and builds without running user code. Its result has the same `exitCode`, `output`, `timedOut`, `truncated`, and `environmentReset` shape. Build time is limited to 60 seconds and does not overwrite existing result objects.

언어별 실행 설정에 `executionMode`를 추가합니다. 기본 `interactive`는 기존 동작을 유지하며, `precompiled`에서는 사전 빌드된 동일 코드·옵션의 결과물만 실행합니다. 결과물이 없거나 코드·옵션이 바뀌면 다시 빌드하라는 오류를 반환하며 자동 컴파일하지 않습니다. C/C++은 네이티브 실행 파일(main 필수), Java는 javac 클래스(main 필수), Python은 버전별 pyc, JavaScript는 V8 코드 캐시, Shell은 구문 검사된 스크립트를 사용합니다. JavaScript 런타임 JIT는 여전히 발생할 수 있고 Shell은 컴파일 언어가 아닙니다. 이 모드는 코드 객체 간 메모리를 공유하지 않으며 결과→코드 표준 입력 연결은 유지합니다. 결과물은 VM 재시작·실행 환경 초기화 후에도 유지되고 VM 삭제 시 함께 삭제됩니다. `interactive` 코드에 대한 빌드는 실행 없이 건너뜁니다.

### 공동 문제 객체와 수동 채점

기존 객체 API에서 `kind: "question"`을 지원합니다. `questionType`은 `choice`(단일 선택 객관식) 또는 `short-answer`(주관식)입니다. `questionPrompt`, `questionOptions`(2~10개), `questionAnswer`, `questionSubmitted`, `questionMaxScore`(정수 1~1000), `questionScore`(-1은 미채점, 채점 시 0~배점), `questionFeedback`, `questionVersion`, `questionGradedVersion`을 저장합니다. 문제·답안·배점·제출 여부 변경 시 새 `questionVersion`을 전달해야 하며 채점 시 `questionGradedVersion`이 현재 버전과 같아야 합니다. 오래된 채점은 `QUESTION_GRADING_STALE`, 버전 없이 내용 변경은 `QUESTION_REVISION_REQUIRED`로 거부합니다. 점수와 문항·보기·답안 크기도 서버에서 검증합니다.

답안 하나와 수동 점수·피드백을 참가자들이 공유합니다. 자동 채점은 하지 않습니다. 수정·제출·채점 모두 해당 객체의 기존 그룹별 쓰기 권한과 잠금 정책을 적용합니다. 브로커는 C++ 저장 성공 후 브로드캐스팅하며 새 참가자도 저장된 내용을 조회합니다. 문제 저장 시 공동 답안과 채점을, 답안 재제출 시 채점을 초기화하도록 클라이언트가 관련 필드와 새 버전을 함께 보냅니다.

### Canvas visibility and password policy

- `private` (also the omitted/default visibility) accepts only the owner and invited participants. Nonempty `canvasPassword`/password updates are rejected; legacy private passwords are not required for entry.
- `public` is searchable by signed-in users and supports an optional password. Password verification is required before issuing a canvas access token; a password does not change visibility to private.
- Switching visibility clears a private canvas's legacy password; switching to private clears any public password in the same settings revision.
- Canvas administrators can invite/remove participants. Removing an invitation removes all group memberships, updates active session ACLs via the settings revision and closes only sessions that lost canvas access. A removed participant cannot reenter a private canvas until invited again. The owner cannot be removed.

### Live canvas settings (2026-11-04)

Successful settings writes no longer broadcast `server_reconnect`. For ordinary settings the broker updates the session revision and sends `canvas_settings_changed` directly, without reading all objects per participant. For ACL changes it serializes a server-only `canvas_access_refresh` for each data session, updates both canvas and RTC identities, then dispatches queued requests with their current identity. Clients receive `canvas_access_changed` with settings and read/write/execute/member groups. ACL changes additionally request a filtered snapshot on the same socket; name/description/password updates do not reload objects. RTC peer IDs and connection proofs remain intact. Sessions denied access receive `canvas_access_revoked` and close without retrying. The server-only refresh event is not accepted from public clients.

Password grants use `password-revision` (default 0), separate from `settings-revision`. Unrelated settings changes retain the grant; password changes and visibility transitions that clear a password increment the password revision. Existing token expiry rules continue to apply.

Python SDK 0.1.3 supports live capability changes and terminal revocation notifications.

### 실시간 실행 출력

The legacy `POST /api/canvases/{id}/runtime/execute/stream` endpoint remains for API clients and returns `application/x-ndjson`. Canvas UI execution receives output chunks as correlated `runtime_output` frames and the final worker result as `runtime_result` on the authenticated canvas WebSocket. Blueprint output nodes display arriving chunks and persist the completed result through the normal broker canvas operation; execution is not retried if the WebSocket disconnects after submission.

### Python SDK 0.1.4: real participant presence

`CanvasSession.connect()` joins both `/canvas/{id}` and `/rtc/canvas/{id}` using the same authenticated canvas access token and the main connection's server-issued binding. Bots and user accounts appear in the browser participant list. No media or P2P connection is started. `peers`, `peer_id`, and `presence_connected` expose membership; `send_cursor`, `send_laser`, and `set_editing` use the existing broker `canvas_relay` protocol. `next_event()` exposes membership and authenticated relays alongside durable changes, chat and comments. Object editing presence is filtered to readable peers; durable object ACLs remain server-enforced. Both channels rejoin after token expiry or connection loss, with no replay of transient presence. Cursor events are coalesced by peer, separate from the bounded document event queue; `event_overflow` reports document queue overflow and `items` remains the latest snapshot. See `sdk/python/examples/watch_canvas.py`.

### VM terminal objects

Create a `shape` object with `shapeType: "rectangle"`, `runtimeRole: "terminal"`, width/height and normal group permissions. The toolbar provides a Terminal tool. Terminal control uses authenticated `POST /api/canvases/{id}/runtime/terminal` with `{objectId, operation, data, cols, rows, offset, passwordGrant}`. Operations are `open`, `poll`, `input`, `resize`, `close`. Data is UTF-8 input (8KiB/request); output is base64 bytes with an absolute `offset`, `truncated`, `exited`, `exitCode`. Poll cursors are independent. The server derives session ownership from the authenticated user; there is no client-supplied owner override. Every request revalidates visibility/password grants and the target object's read/execute group intersection. Cached authorization reads only metadata and the target object's kind, role and permissions through the existing storage broker.

Shells are private to `(canvas,user,object)` and share `/workspace` with code cells. They run as VM UID 1000 with a real controlling PTY, support Ctrl+C, resize, interactive prompts and persistent shell state. Python/pip wrappers use the selected Python runtime. Terminal output and credentials are never saved into object fields or broadcast. Up to 8 shells per VM, 256KiB recent output/session, 120 seconds without requests until shell cleanup. Explicit disconnect terminates the shell; viewport removal leaves it resumable until idle expiry. VM deletion removes all its sessions and disk. No SSH/public VM ingress is opened.

### WebSocket terminal transport

The browser uses authenticated `POST /api/canvases/{id}/runtime/terminal/ticket` with `{objectId,passwordGrant}` once per connection. The response contains `{ticket,expiresIn:15,path:"/api/terminal/ws"}`. Connect via WSS and send `{type:"attach",ticket,cols,rows,offset}` as the first frame within 5 seconds. Tickets are single-use and IP-bound; neither account tokens nor tickets appear in the WebSocket URL. Existing Origin policy applies. The server derives canvas, object and user from the ticket.

Input frames: `{operation:"input"|"resize"|"close",data,cols,rows}`. Server frames: `{type:"ready"|"output",data:<base64>,offset,truncated,exited,exitCode}`, `{type:"closed",closed:true}`, or `{type:"error",retryable:false,message}`. Input is sent immediately. Output is pushed over a persistent Spring → Unix socket manager → guest VSOCK channel; no browser HTTP polling is used. Heartbeats keep idle connections alive. Brief transport failures reissue a ticket and resume the output cursor, without replaying potentially executed input. Every 2 seconds the server checks current token/account/MFA and object read/execute permissions; revoked access closes the connection with 1008.

Manager stream slots are separate from normal code execution slots. Existing running guests without stream support use an internal compatibility bridge until their next ordinary boot; connecting via WebSocket does not force their restart. The legacy HTTP terminal endpoint remains for compatibility, while the canvas UI uses WSS.

Validation (2026-11-04): actual public WSS browser connection to an isolated KVM verified UID/workspace, shell variables, Ctrl+C, reconnect and zero HTTP terminal polling. Local public-route echo: median 17.04ms, p95 19.39ms (20 keys; not a guarantee of external-network latency). Raw manager/guest stream: median ~10.5ms. Real bot tests rejected disjoint object groups, foreign Origins, ticket replay/expiry, and closed an active connection after permission revocation.

### 공유 값 객체와 미디어 상태

객체 `kind: "value"`는 계산이나 코드 실행 없이 값을 공유합니다. `valueType`은 `real`(실수), `integer`, `button`(0/1), `select`(선택지 인덱스), `text`입니다. `valueNumber`, `valueText`, `valueMin`, `valueMax`, `valueStep`, `valueOptions`, `title`을 기존 객체 변경 API/브로커 operations의 patch로 저장합니다. 범위는 min < max, step > 0이며 숫자 값은 범위 안에 있어야 합니다. 정수 필드는 정수여야 합니다. 선택지는 1~32개이고 인덱스는 유효 범위 안이어야 합니다. 일반 객체와 동일한 그룹 읽기·쓰기 권한을 적용합니다.

PDF 객체의 `pdfPage`는 1 이상의 정수입니다. 영상 링크의 `mediaState`는 `{paused: boolean, time: number, updatedAt: number, actor: string}`이며 위치와 시각은 음수가 아닌 유한수입니다. 코드/실행 객체의 `runtimeState`는 공유 진행 상태입니다. 정확히 실행 상태만 변경하는 요청에는 실행 권한을 사용하며 일반 내용 수정 권한을 허용하지 않습니다. 해당 상태와 출력 결과는 저장 승인 후 공유되고 신규 접속 초기 문서에도 포함됩니다. 음량과 화면 배치는 개인 설정이며 셸 입출력은 개인 세션입니다.

### Jupyter 노트북 객체

`POST /api/canvases/{canvasId}/runtime/notebook` remains for API clients and environment `prepare`/`reset`. Canvas UI sends cell execution as `runtime_execute` with `action: "notebook"` over the canvas WebSocket. The C++ broker validates both the notebook root and the selected cell object's execute ACL before forwarding to the VM manager Unix socket. The selected Python IPython/Jupyter kernel returns `{outputs, executionCount, exitCode, timedOut?, truncated?, error?, engine}` to the C++ broker, which returns it as `runtime_result` to the requester over the same WebSocket. `outputs` use standard notebook `stream`, `execute_result`, `display_data`, and `error` forms. Supported display MIME types are text/plain, text/html, image/png, image/jpeg, and image/svg+xml. Cells have a 15-second execution limit and do not accept stdin.

노트북 도구 객체는 `kind: "shape", runtimeRole: "notebook"`이고 셀 ID 순서를 `notebookCellIds: [cellObjectId, ...]`로 보관합니다. 각 셀은 별도 캔버스 객체인 `kind: "shape", runtimeRole: "notebook-cell"`이며 `{notebookId, notebookCell: {id, cellType, source}}`를 저장합니다. 출력은 해당 셀 객체의 `notebookResult`에 저장됩니다. 셀 편집은 그 셀 객체만 패치·브로드캐스트하고, 셀 추가·삭제·순서 변경 때만 노트북의 ID 목록도 바뀝니다. 따라서 다른 셀의 소스나 출력은 매 편집마다 다시 전송되지 않습니다.

셀 객체는 노트북 root와 같은 그룹 권한을 사용하고 같은 노트북 그룹으로 함께 이동·복사됩니다. 최대 32셀, 셀 소스는 UTF-8 64KB 이하이며 셀 결과 객체는 512KB 이하입니다. `notebookResult`만 바꾸는 정확한 patch는 실행 권한으로 허용합니다. 내용·권한·위치를 함께 바꾸면 일반 쓰기 권한과 잠금 검사가 적용됩니다. 저장 승인 후 기존 브로커 경로로 개별 셀 객체가 브로드캐스트되며 신규 참가자는 권한 필터링된 노트북·셀 객체 스냅샷을 받습니다. 예전 `notebookCells` 배열 형식으로 저장된 노트북은 캔버스 관리자 클라이언트가 접속하면 셀 객체로 한 번 변환합니다. Python 커널 메모리 상태는 VM/런타임 수명에 종속되며 노트북별 최근 4개 커널을 유지합니다.

### GitHub 브랜치 캔버스 연결

GitHub 인증/커밋/병합은 브라우저에서 수행한다. 아래 API는 FreLog 인증을 사용하며 GitHub 토큰을 받지 않는다.

- `GET /api/canvases/{id}/github`: 현재 방과 루트 방의 읽기 권한을 확인해 `rootId`, `currentBranch`, `project: {target:{owner,repo,path},branches:{branch:{canvasId,initialized}}}`를 반환한다.
- `POST .../github/bind`: 관리자만 `{owner,repo,path,branch}`로 첫 프로젝트와 현재 방의 브랜치를 연결한다. 기존 연결 변경도 루트 관리자만 수행하며 브랜치 방 연결을 보존한다.
- `POST .../github/branches`: 작업 권한이 있는 사용자가 `{branch}`로 기존 방을 조회하거나 원본 ACL·공개 여부·비밀번호를 복사한 방을 생성한다. 최대 128개. 동일 브랜치는 기존 ID를 반환한다.
- `POST .../github/initialized`: `{branch}` 방의 초기 객체가 브로커에서 저장 확인된 뒤 초기화 완료로 표시한다.

연결 정보는 영구 리소스 볼륨의 내부 JSON 파일에 저장하며 파일 잠금과 원자적 교체를 사용한다. 캔버스 객체는 이 API에서 직접 저장하지 않으며 기존 인증된 브로커 WebSocket을 사용한다.

#### GitHub 그룹 작업 권한

- `POST /api/canvases/{id}/github/permissions`: 루트 관리자만 `{groups:{group:[commit,push,pr,create_branch,switch_branch]}}`를 저장한다. 생략된 일반 그룹은 거부한다.
- `POST .../github/authorize`: `{action,owner,repo,path,branch}`를 받아 현재 사용자 그룹과 서버의 연결 대상을 재검사한다. 일반 사용자는 위임 작업만 허용하며 관리자는 복구/병합도 허용한다.
- context는 `capabilities`, `groups`, `readGroups`와 프로젝트의 `permissions`를 반환한다. 연결 변경은 관리자만 가능하다.
- branches 요청에 `canvas_password_token`을 포함할 수 있다. 보호된 원본의 유효한 입장 인증을 확인하고 대상과 같은 비밀번호 해시인 경우 응답에 대상 전용 입장 인증을 반환한다. 브랜치 방은 원본 공개 여부/비밀번호를 상속한다.
- GitHub 토큰은 이 API에 전달하지 않는다. 브라우저 보관 기한은 24시간이며 GitHub 자체의 만료/권한 검사는 별도로 적용된다. 클라이언트 PAT의 외부 사용은 FreLog 정책으로 통제할 수 없다.

비밀번호 인증 토큰은 최초 비밀번호 확인 시점부터 4시간 동안 유효합니다. 접속 중 토큰을 갱신하는 별도 API는 없으며, 토큰 재사용은 만료 시각을 연장하지 않습니다. 비밀번호 원문은 브라우저에 저장하지 않습니다.

## 캔버스 VM WebSocket 관리 (2026-11-12)

현재 캔버스 클라이언트의 VM 설정·리소스·디스크·Java 의존성·커널 초기화와
노트북 준비·초기화는 브로커 WebSocket을 사용합니다. 기존 Spring HTTP API는
호환 경로로 남아 있으며 Spring에서 독립적으로 권한을 확인합니다.
`runtime_execute.action=manage`의 management 필드와 권한은 Wall의
BROKER_PROTOCOL.md를 참조하세요. C++은 연결된 캔버스와 인증된 사용자로
요청을 검증한 후 Unix 소켓으로 VM 관리자에 전달합니다.
