# Project Agora API·WebSocket 명세

이 문서는 현재 소스 코드의 Spring Boot API, C++ 제어 API, 테스트 프록시와 캔버스 WebSocket 프로토콜을 기준으로 작성되었습니다.

## 공통 규칙

- Spring API 기본 주소: `http(s)://<spring-host>:8080`
- C++ 제어 API 기본 주소: `http(s)://<cpp-host>:8000`
- 모든 JSON 요청과 응답은 `Content-Type: application/json`을 사용합니다.
- 보호된 Spring API는 `Authorization: Bearer <accessToken>`이 필요합니다.
- 로그인 JWT와 캔버스 접속 JWT는 용도가 다릅니다. WebSocket에는 `/access` 응답의 `canvas_access_token`만 사용합니다.
- 응답의 사용자 공개 식별자는 `nickname`과 `tag_number` 조합입니다. 내부 `user_id`는 권한·세션 처리에만 사용합니다.

### 오류 응답

Spring 오류는 다음 형태입니다.

```json
{
  "code": "CANVAS_001",
  "message": "캔버스를 찾을 수 없습니다.",
  "errors": []
}
```

`errors`는 입력 검증 오류에서만 포함되며 필드별 `field`, `rejectedValue`, `reason`을 담습니다. 주요 상태 코드는 `400`(잘못된 입력), `401`(인증 실패), `403`(권한/비밀번호), `404`(리소스 없음), `409`(중복·동시성 충돌), `500`(서버 오류)입니다.

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
| `GET` | `/api/users` | 없음 | 사용자 배열 |
| `GET` | `/api/users/{nickname}/{tagNumber}` | 필요 | 사용자 1명 |
| `PUT`, `PATCH` | `/api/users/{nickname}/{tagNumber}` | 필요 | `{ "nickname":"새 이름", "password":"새 비밀번호" }` (각 필드 선택) |
| `DELETE` | `/api/users/{nickname}/{tagNumber}` | 필요 | 성공 시 `204` |

사용자 수정·삭제는 서비스의 현재 권한 정책(본인 또는 관리자)을 따릅니다.

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

목록 응답은 `CanvasSummaryResponse` 배열입니다.

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

변경 API는 캔버스가 비활성 상태일 때 관리자/소유자 정책으로 사용합니다. 활성 WebSocket 세션에서는 아래 WebSocket 설정 이벤트를 사용합니다.

| 메서드·경로 | 요청 본문 | 성공 |
|---|---|---|
| `PATCH /api/canvases/{canvasId}/name` | `{ "canvas_name":"새 이름" }` | `204` |
| `PATCH /api/canvases/{canvasId}/description` | `{ "description":"새 설명" }` | `204` |
| `PATCH /api/canvases/{canvasId}/password` | `{ "canvas_password":"새 비밀번호" }` (빈 값은 해제) | `204` |
| `POST /api/canvases/{canvasId}/people` | `{ "nickname":"사용자", "tag_number":1 }` | `204` |
| `DELETE /api/canvases/{canvasId}/people` | 위와 동일 | `204` |
| `DELETE /api/canvases/{canvasId}` | 없음 | `204` |

비밀번호는 저장 시 해시화되며 API 응답에 평문·해시를 포함하지 않습니다. 참여자 관리는 닉네임과 태그 번호로 수행합니다. 현재 Spring의 `/access`는 참여자 목록을 검사하지 않지만, C++ WebSocket handshake는 Redis의 `people`과 설정 revision을 다시 검사합니다(자세한 내용은 WebSocket 절 참조).

### 캔버스 접속 정보 발급

`POST /api/canvases/{canvasId}/access`

비밀번호가 있는 경우:

```json
{"password":"캔버스 비밀번호"}
```

비밀번호가 없으면 본문을 생략하거나 `{}`를 보냅니다. 성공 응답:

```json
{"server_id":5,"ws_port":"8002","canvas_access_token":"<canvas-jwt>"}
```

응답의 `ws_port`와 토큰으로 WebSocket을 연결합니다. 서버 선택은 활성화 상태, 최근 15초 heartbeat, `/health` 확인을 만족하는 C++ 서버만 대상으로 합니다.

## 3. 로드 밸런서·내부 API

현재 공개된 Spring 경로는 다음과 같습니다. 운영에서는 관리자 또는 내부 네트워크에서만 노출해야 합니다.

| 메서드·경로 | 응답 |
|---|---|
| `GET`, `POST /api/load-balancer/allocate/server` | `{ "ip","port","wsPort","serverIp","serverPort" }` |
| `GET`, `POST /api/load-balancer/allocate/redis` | `{ "ip","port","redisIp","redisPort" }` |
| `GET /api/load-balancer/database` | `{ "ip","port","dbHost","dbPort","dbName","address" }` |
| `POST /api/load-balancer/allocate/database` | 위와 동일 |
| `GET /api/database/address` | 위와 동일(별칭) |

### 테스트 페이지 프록시 API

| 메서드·경로 | 요청 |
|---|---|
| `POST /api/test/cpp-access` | `{ "server_ip":"127.0.0.1", "server_port":8000, "canvas_id":1 }`; `Authorization`을 C++로 전달 |
| `POST /api/test/cpp-disconnect` | 위 필드 + `user_id`; C++ 연결 종료 프록시 |
| `GET /api/test/cpp-canvas-count?host=127.0.0.1&port=8000` | 활성 캔버스 수 |
| `GET /api/test/cpp-active-canvases?host=127.0.0.1&port=8000` | 활성 캔버스 상세 |
| `GET /api/test/socket-ping?host=127.0.0.1&port=8002&timeoutMs=3000` | TCP 연결·지연시간 점검 |

이 프록시는 DB에 등록되어 있고 최근 heartbeat가 있는 C++ 서버만 대상으로 합니다.

## 4. C++ HTTP 제어 API

기본 REST 포트는 `8000`입니다. 다음 API는 서비스 간 제어용이며 일반 브라우저에 직접 공개하지 않습니다.

| 메서드·경로 | 응답 |
|---|---|
| `POST /api/users/{userId}/disconnect` | `{"status":"success","message":"User disconnected"}` |
| `POST /api/canvas/{canvasId}/users/{userId}/disconnect` | `{"status":"success","message":"User disconnected from canvas"}` |
| `DELETE /api/canvas/{canvasId}` | `{"status":"success","canvas_id":1,"removed":true}` |
| `GET /api/canvas/count` | `{"status":"success","count":1}` |
| `GET /api/canvas/active` | `{"status":"success","count":1,"canvases":[...]}` |
| `GET /health` | `{"status":"UP","service":"Agora C++ Realtime Server"}` |
| `GET /` | `{"status":"online","service":"Agora C++ Server"}` |

`POST /api/access`와 `POST /api/access/disconnect`는 현재 제거되어 더 이상 지원되지 않습니다.

## 5. WebSocket 프로토콜

### 연결

Nginx 경유:

```text
wss://<domain>/wss/port/<wsPort>/canvas/<canvasId>?token=<canvasAccessToken>
```

직접 연결:

```text
ws://<cpp-host>:<wsPort>/ws/canvas/<canvasId>?token=<canvasAccessToken>
```

`canvas_id`는 경로 또는 쿼리(`canvas_id`/`canvasId`)로 전달할 수 있습니다. 토큰 검증에는 HS256 서명, `canvasId`, `clientIp`, `serverHash`, `nickname`, `tagNumber`, `settingsRevision`이 사용됩니다. 실패 시 handshake가 `400` 또는 `401`로 거부됩니다.

연결 성공 후 서버는 다음 초기 이벤트를 보냅니다.

```json
{
  "type":"init_items",
  "canvas_id":1,
  "user_id":1,
  "server_protocol":"uWebSockets",
  "status":"connected",
  "items":{},
  "inner-group":{},
  "canvas_name":"이름"
}
```

`items`는 접속자의 permission 그룹으로 필터링됩니다. `user_id`는 초기 내부 호환 필드이며 채팅 공개 payload에는 포함하지 않습니다.

### 일반 이벤트

#### 채팅

클라이언트 → 서버:

```json
{"type":"chat","text":"test"}
```

서버 → 같은 캔버스의 다른 접속자:

```json
{"type":"chat","text":"test","sender":"아고라관리자","tag_number":1,"canvas_id":1}
```

서버가 `sender`, `tag_number`, `canvas_id`를 인증된 소켓 정보로 덮어씁니다. `user_id`·`sender_id`를 보내더라도 채팅 payload에서 제거됩니다.

#### 캔버스 아이템

아이템 이벤트는 임의 JSON 객체에 `type`과 `item_id`(또는 `item-id`)를 포함합니다. 아이템 데이터는 `item` 또는 `data` 필드에 넣고, 전체 갱신은 `items` 객체를 사용합니다. 예:

```json
{"type":"item_update","item_id":"note-1","item":{"x":10,"y":20,"text":"hello","permission":"default"}}
```

삭제 이벤트 타입은 `item_delete` 또는 `delete_item`입니다. 서버는 소켓의 그룹 권한을 검사하고 거부 시 다음을 보냅니다.

```json
{"type":"error","code":"ITEM_ACCESS_DENIED"}
```

허용된 이벤트는 발신자를 제외한 같은 캔버스 접속자에게 전달되고 RedisJSON에 저장됩니다. `ping`, `pong`, `chat`은 영속화하지 않습니다. 초당 100개를 초과하는 메시지는 버려집니다.

#### 연결 상태 확인

```json
{"type":"ping"}
```

응답:

```json
{"type":"pong","canvas_id":1,"user_id":1,"timestamp":1700000000}
```

### 설정 이벤트

활성 WebSocket에서 캔버스 설정을 읽거나 변경할 수 있습니다. 설정 접근은 현재 참여자(`people`)이며 `canvas_settings_update`는 `admin-group` 멤버만 허용됩니다.

#### 설정 조회

```json
{"type":"canvas_settings_get","request_id":"req-1"}
```

응답:

```json
{"type":"canvas_settings_snapshot","settings":{"canvas_id":1,"canvas_name":"이름","description":"설명","password_protected":false,"settings_revision":3,"participants":[{"nickname":"사용자","tag_number":1}]}}
```

#### 설정 변경

모든 변경은 현재 revision을 낙관적 잠금 값으로 보내야 합니다.

```json
{"type":"canvas_settings_update","request_id":"req-2","expected_revision":3,"field":"name","value":"새 이름"}
```

`field`별 payload:

| field | 추가 필드 | 설명 |
|---|---|---|
| `name` | `value` 문자열 | 이름 변경 |
| `description` | `value` 문자열 | 설명 변경 |
| `password` | `value` 문자열 | 비밀번호 설정/변경(평문은 응답에 반환하지 않음) |
| `participant_add` | `nickname`, `tag_number` | 참여자 추가 |
| `participant_remove` | `nickname`, `tag_number` | 참여자 제거(소유자 제거 불가) |

성공 응답:

```json
{"type":"canvas_settings_result","ok":true,"request_id":"req-2","settings":{...}}
```

다른 접속자에게는 `canvas_settings_changed` 이벤트가 전달됩니다.

```json
{"type":"canvas_settings_changed","settings":{...}}
```

가능한 실패 코드에는 `CANVAS_NOT_READY`, `SETTINGS_STORAGE_ERROR`, `SETTINGS_ACCESS_DENIED`, `SETTINGS_INVALID_INPUT`, `SETTINGS_REVISION_REQUIRED`, `SETTINGS_CONFLICT`, `SETTINGS_USER_NOT_FOUND`, `SETTINGS_ALREADY_PARTICIPANT`, `SETTINGS_OWNER_REQUIRED`가 있습니다. `SETTINGS_CONFLICT`에는 최신 `settings`가 함께 반환되므로 이를 반영해 새 revision으로 재시도해야 합니다.

### 종료·재접속

WebSocket close 시 C++ 서버가 사용자 세션을 비활성화합니다. 캔버스 row나 캔버스 문서는 삭제하지 않습니다. 서버 종료 시에도 연결 종료와 세션 정리만 수행하며, 캔버스 삭제는 별도의 `DELETE /api/canvas/{canvasId}` 제어 API입니다. 설정 revision 변경으로 기존 토큰이 무효화되면 서버는 close code `1008`로 연결을 종료하고 새 `/access` 토큰을 요구합니다.

## 6. 상태·운영 참고

- C++는 시작 시 `cpp_server`를 등록/활성화하고 5초마다 `last_heartbeat_at`을 갱신합니다. 정상 종료 시 row를 삭제하지 않고 비활성화합니다.
- Spring은 서버 선택 시 15초 이내 heartbeat와 C++ `/health` 응답을 모두 확인합니다.
- WebSocket 캔버스 세션은 Redis의 최신 문서를 기준으로 초기화하고, 마지막 접속자가 나가면 캐시를 해제하고 영속 저장 흐름을 수행합니다.
- `people`는 참여자·설정 권한의 내부 목록입니다. Spring의 캔버스 접속 정보 발급은 이 목록을 검사하지 않지만, 현재 C++ WebSocket handshake는 목록에 포함된 사용자만 최종 연결시킵니다.
