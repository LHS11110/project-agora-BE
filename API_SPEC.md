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

변경 API는 캔버스가 비활성 상태일 때 관리자 정책으로 사용합니다. 활성 캔버스에 Spring API로 변경 요청을 보내면 `409 CANVAS_006`과 함께 캔버스에 접속해 설정에서 변경하라는 안내를 반환합니다. 활성 WebSocket 세션에서는 아래 WebSocket 설정 이벤트를 사용하며, 성공한 변경은 Redis와 Elasticsearch에 동기 반영됩니다.

| 메서드·경로 | 요청 본문 | 성공 |
|---|---|---|
| `PATCH /api/canvases/{canvasId}/name` | `{ "canvas_name":"새 이름" }` | `204` |
| `PATCH /api/canvases/{canvasId}/description` | `{ "description":"새 설명" }` | `204` |
| `PATCH /api/canvases/{canvasId}/password` | `{ "canvas_password":"새 비밀번호" }` (빈 값은 해제) | `204` |
| `POST /api/canvases/{canvasId}/people` | `{ "nickname":"사용자", "tag_number":1 }` | `204` |
| `DELETE /api/canvases/{canvasId}/people` | 위와 동일 | `204` |
| `DELETE /api/canvases/{canvasId}` | 없음 | `204` |

비밀번호는 저장 시 해시화되며 API 응답에 평문·해시를 포함하지 않습니다. 참여자 관리는 닉네임과 태그 번호로 수행합니다. Spring의 `/access`는 로그인·캔버스 비밀번호를 확인하고 접속 토큰을 발급합니다. 참여 권한은 C++ WebSocket에서 캐시·세션 예약 전에 한 번 확인하며, 로드 후에는 revision만 비교해 그 사이의 설정 변경 경합을 차단합니다.

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

`GET /api/test/cpp-active-canvases`는 로그인한 사용자가 테스트베드에서 활성 상태를 확인할 수 있도록 인증된 사용자에게 허용됩니다. 그 밖의 `/api/test/**` 프록시 API는 관리자 전용입니다.

| 메서드·경로 | 요청 |
|---|---|
| `POST /api/test/cpp-access` | `{ "server_ip":"127.0.0.1", "server_port":8000, "canvas_id":1 }`; `Authorization`을 C++로 전달 |
| `POST /api/test/cpp-disconnect` | 위 필드 + `user_id`; C++ 연결 종료 프록시 |
| `GET /api/test/cpp-canvas-count?host=127.0.0.1&port=8000` | 활성 캔버스 수 |
| `GET /api/test/cpp-active-canvases?host=127.0.0.1&port=8000` | 활성 캔버스 상세 |

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

캔버스 기능은 캔버스 WebSocket 하나가 송신과 수신을 모두 처리합니다. WebRTC 기능을 사용할 때는 RTC 신호 WebSocket을 별도로 추가 연결합니다. 둘은 같은 C++ WebSocket 포트를 사용하지만 URL 경로에 따라 핸들러가 나뉘며, 독립된 두 연결입니다.

| 목적 | Nginx 경유 | C++ 직접 연결 |
|---|---|---|
| 캔버스 이벤트 송수신 | `wss://<domain>/wss/port/<wsPort>/canvas/<canvasId>?token=<canvasAccessToken>` | `ws://<cpp-host>:<wsPort>/ws/canvas/<canvasId>?token=<canvasAccessToken>` |
| RTC 신호 교환 | `wss://<domain>/wss/port/<wsPort>/rtc/canvas/<canvasId>?token=<canvasAccessToken>` | `ws://<cpp-host>:<wsPort>/ws/rtc/canvas/<canvasId>?token=<canvasAccessToken>` |

Nginx는 외부 `/wss/port/...` 요청을 같은 `<wsPort>`의 C++ 서버로 전달하면서 경로를 `/ws/...`로 바꿉니다. 두 연결 모두 `/api/canvases/{canvasId}/access`에서 받은 캔버스 접속 토큰을 사용합니다. C++ 직접 연결은 경로의 `<canvasId>`를 씁니다. 경로 ID가 없는 C++ 별칭 `/ws/canvas`와 `/ws/rtc/canvas`는 `?canvas_id=` 또는 `?canvasId=`도 받을 수 있지만, Nginx 경로는 캔버스 ID를 경로에 넣어야 합니다.

캔버스 이벤트는 캔버스 WebSocket 하나로 양방향 송수신합니다. 예전 RX/TX TCP 소켓 구현은 제거되었습니다.

토큰 검증에는 HS256 서명, `canvasId`, `clientIp`, `serverHash`, `nickname`, `tagNumber`, `settingsRevision`이 사용됩니다. 캔버스 ID가 없거나 잘못되면 `400`, 토큰이 누락되거나 유효하지 않으면 `401`로 upgrade를 거부합니다. 캔버스 WebSocket은 참여 권한 검사와 로드가 성공한 뒤 `init_items`를 보내며, RTC 신호 WebSocket은 연결 직후 `rtc_ready`를 보냅니다. RTC 연결 자체는 Canvas 로드나 SQL `user_sessions` 변경을 하지 않습니다.

연결 성공 후 서버는 다음 초기 이벤트를 보냅니다.

```json
{
  "type":"init_items",
  "canvas_id":1,
  "rtc_canvas_connection_id":"12345",
  "rtc_canvas_connection_hash":"<64-char-hex-proof>",
  "server_protocol":"uWebSockets",
  "status":"connected",
  "items":{},
  "groups":["default"],
  "canvas_name":"이름"
}
```

`items`는 접속자의 permission 그룹으로 필터링됩니다. `groups`는 현재 사용자가 속한 공개 그룹명만 담습니다. 내부 사용자 ID와 전체 그룹 구성원 목록은 전송하지 않습니다.

`rtc_canvas_connection_id`와 `rtc_canvas_connection_hash`는 해당 캔버스 WebSocket 연결에 묶인 쌍입니다. 해시는 연결별 난수 키로 ID를 HMAC-SHA256 처리한 64자리 16진수 증명값이며, 캔버스 WebSocket 연결마다 새로 생성됩니다.

### 일반 이벤트

#### 채팅방과 내역

각 채팅방은 `items[room_id]`의 `chat_room` 아이템이며, 실제 메시지 내역은 그 아이템의 `data` 배열에 순번과 함께 저장됩니다. 여러 방은 서로 다른 `room_id`를 사용합니다. 아이템 조회 초기 이벤트에는 방 메타데이터만 포함되고 `data`는 제외되므로, 필요한 내역을 별도로 요청합니다.

Elasticsearch의 기존 `items.*` 필드 매핑과 임의 형식의 아이템이 충돌하지 않도록, 영속 문서에는 `items` 객체 대신 JSON을 분할 인코딩한 `items-b64` 배열을 저장합니다. C++과 Spring은 Elasticsearch 문서를 읽을 때 이를 다시 `items` 객체로 복원합니다. 기존 `items` 객체로 저장된 문서도 계속 읽을 수 있습니다. Redis와 WebSocket/API의 `items` 형식은 그대로입니다.

방은 일반 아이템처럼 미리 만들 수 있습니다.

```json
{"type":"item_update","item_id":"general","item":{"type":"chat_room","permission":["default"],"data":[]}}
```

아직 없는 `room_id`로 처음 메시지를 보내면 서버가 채팅방 아이템을 만들고, 생성자의 현재 그룹을 `permission`으로 사용합니다. 기존 방에 대한 메시지 쓰기와 내역 조회는 방 아이템의 ACL을 따릅니다. 빈 ACL은 관리자만 접근할 수 있습니다. 일반 사용자는 채팅방 아이템을 직접 수정하거나 삭제할 수 없습니다.

채팅 내역은 계정 역할별로 자동 분리되지 않고 캔버스와 `room_id`를 기준으로 저장됩니다. C++ WebSocket의 `admin-group` 멤버는 방 권한을 우회해 모든 방의 실시간 메시지와 내역을 볼 수 있고, 일반 사용자는 자신이 속한 그룹이 허용된 방만 볼 수 있습니다. 관리자 전용 대화가 필요하면 `admin-group` 권한의 별도 방을 만들고, 일반 대화에는 일반 그룹 권한을 지정해야 합니다.

테스트베드는 연결 후 `init_items.items`에서 사용자에게 전달된 `type: "chat_room"` 아이템을 채팅방 선택 목록에 표시합니다. 각 항목의 ID가 `room_id`, `permission`이 방 권한입니다. 초기 아이템에서는 메시지 배열(`data`)을 생략하므로, 선택한 방의 최근 메시지는 `chat_history` 요청으로 불러옵니다.

캔버스의 `admin-group` 멤버에게는 테스트베드의 채팅방 만들기 버튼이 표시됩니다. 방 ID와 허용할 그룹을 지정해 `chat_room` 아이템을 생성하며, 권한 입력을 비우면 `admin-group` 전용 방을 만듭니다.

메시지 보내기:

```json
{"type":"chat","room_id":"general","text":"안녕하세요","request_id":"chat-17"}
```

서버는 인증된 `sender`, `tag_number`, `canvas_id`와 방별 1부터 시작하는 `sequence`, `created_at`을 붙여 같은 ACL을 가진 접속자에게 보냅니다. 클라이언트는 `sender`와 `tag_number`를 자신의 공개 식별자와 비교해 내 메시지를 구분합니다. 내부 `sender_user_id`는 저장 처리에만 사용하고 WebSocket 채팅 이벤트와 내역 조회 응답에서 제외합니다. 기존 저장 내역의 `sender_user_id`도 응답 전에 제거합니다. 클라이언트가 보낸 사용자 식별 값은 사용하지 않습니다. 서버는 메시지를 Canvas별 FIFO 저장 큐에 넣고 Redis 저장 완료를 기다리지 않은 채 브로드캐스트를 진행합니다. background worker가 RedisJSON의 해당 아이템 `data`에 저장하며 배열 추가와 다음 순번 갱신은 한 Lua 스크립트 안에서 원자적으로 처리됩니다.

최근 메시지 N개 조회 (기본 50개, 최대 200개, 응답은 오래된 순서부터):

```json
{"type":"chat_history","room_id":"general","limit":50,"request_id":"history-1"}
```

1부터 시작하는 순번 범위를 양 끝 포함으로 조회할 수 있습니다. 한 번에 최대 200개 순번 범위를 요청할 수 있으며, 범위 조회에는 `limit`을 함께 보내지 않습니다.

```json
{"type":"chat_history","room_id":"general","from_sequence":21,"to_sequence":40,"request_id":"history-2"}
```

성공 응답은 `messages`, 실제 반환 범위인 `from_sequence`/`to_sequence`, 전체 메시지 수인 `total`, 커서 방향으로 더 읽을 내역이 있는지를 나타내는 `has_more`를 포함합니다. 최근 N개 조회에서 `has_more`가 참이면 `next_to_sequence`로 더 오래된 메시지를 요청할 수 있습니다. `from_sequence`만 지정하면 `next_from_sequence`, `to_sequence`만 지정하면 `next_to_sequence`가 반환됩니다. 오류는 `error` 이벤트와 `CHAT_ROOM_NOT_FOUND`, `ITEM_ACCESS_DENIED`, `CHAT_INVALID_RANGE` 등의 코드로 전달됩니다.

저장되는 방 아이템은 다음 구조를 가집니다. `sequence`는 방별로 1부터 증가하며, `data` 배열도 그 순서로 append됩니다. 예시의 내부 `sender_user_id`는 클라이언트 응답에서 제외됩니다.

```json
{
  "type":"chat_room",
  "permission":["default"],
  "next_sequence":3,
  "data":[
    {"sequence":1,"text":"안녕하세요","sender":"아고라관리자","tag_number":1,"sender_user_id":123,"created_at":1700000000000},
    {"sequence":2,"text":"반가워요","sender":"사용자","tag_number":2,"sender_user_id":456,"created_at":1700000001000}
  ]
}
```

#### 캔버스 아이템

아이템 이벤트는 임의 JSON 객체에 `type`과 `item_id`(또는 `item-id`)를 포함합니다. 아이템 데이터는 `item` 또는 `data` 필드에 넣고, 전체 갱신은 `items` 객체를 사용합니다. 예:

```json
{"type":"item_update","item_id":"shape-1","item":{"kind":"shape","x":0.1,"y":0.2,"width":0.2,"height":0.1,"permission":"default"}}
```

삭제 이벤트 타입은 `item_delete` 또는 `delete_item`입니다. 서버는 소켓의 그룹 권한을 검사하고 거부 시 다음을 보냅니다.

```json
{"type":"error","code":"ITEM_ACCESS_DENIED"}
```

ACL은 저장될 `item` 또는 `data` 객체의 `permission` 필드에서 읽습니다. 기존 클라이언트 호환을 위해 top-level `permission`도 받을 수 있지만, payload에 ACL이 이미 있으면 해석된 권한 그룹이 같아야 합니다. ACL이 없거나 비어 있으면 관리자에게만 보입니다. 일반 사용자는 자신이 속한 그룹만 대상으로 새 아이템을 만들 수 있고, 기존 아이템은 현재 ACL이 허용할 때 수정·삭제할 수 있지만 ACL 자체는 바꿀 수 없습니다. 전체 `items` 교체는 관리자만 할 수 있습니다.

아이템을 처음 전달할 때와 실시간 이벤트를 보낼 때 모두 이 ACL을 적용합니다. ACL이 바뀌어 기존 접속자의 권한이 회수되면 서버는 해당 사용자에게 `item_delete`를 보내 로컬 표시에서도 아이템을 제거합니다. 그래픽·이미지 등 비텍스트 아이템은 `item_update`로 저장할 수 있습니다. `text`, `code`, `automerge_snapshot`, `automerge_changes`를 포함하는 이벤트는 `item_update`로 저장하지 않으며 `ITEM_SAVE_REQUIRED`로 거부합니다.

텍스트·노트·코드의 영속 저장은 사용자가 `Ctrl + S`를 눌렀을 때만 발생합니다. 실시간 Automerge 변경은 피어 간 P2P로만 전달되며 서버에는 전송되지 않습니다. 저장 시 클라이언트는 아이템 생성 때 정해진 공통 base 스냅샷과 그 base 이후의 변경 이력을 함께 전송합니다.

```json
{"type":"item_save","item_id":"note-1","item":{"kind":"text","text":"hello","permission":["default"],"automerge_snapshot":"<immutable-base64>","automerge_changes":[{"field":"text","change":"<base64-automerge-change>"}]}}
```

서버는 `item_save`에 한해 payload를 RedisJSON 저장 큐에 넣습니다. 같은 아이템 종류의 기존 base 스냅샷은 바꾸지 않고, 이전 저장 내역과 새 `automerge_changes`를 change 바이트 기준으로 중복 제거해 합칩니다. 이 방식은 서로 독립된 피어가 같은 base에서 만든 오프라인 변경 branch를 보존해 다음 접속 때 Automerge가 병합하게 합니다. 구형 `item_crdt_change` 이벤트는 `ITEM_SAVE_REQUIRED`로 거부됩니다. 전체 `item_save` JSON payload 크기 상한은 12 MiB이며 Automerge change마다 `field`는 텍스트 아이템의 `text`, 코드 아이템의 `code`여야 합니다. 저장 완료는 캔버스 FIFO worker에서 비동기로 수행되므로 WebSocket `pong`은 저장 영속성 확인 응답이 아닙니다.

### WebRTC P2P signaling

RTC 신호 WebSocket은 연결 표의 `/rtc/canvas/<canvasId>` 경로로 엽니다. C++는 JWT를 확인한 뒤 `rtc_ready`를 보냅니다. 이 연결은 캔버스를 로드하거나 SQL `user_sessions`를 변경하지 않으며, 연결만 연다고 피어가 등록되지는 않습니다.

캔버스 WebSocket의 `init_items.rtc_canvas_connection_id`와 `init_items.rtc_canvas_connection_hash`를 함께 RTC 신호 WebSocket의 `rtc_join`에 전달해야 합니다. 서버는 같은 캔버스·사용자의 활성 캔버스 WebSocket을 찾고 ID에 대응하는 해시를 상수 시간 비교로 검증한 뒤 피어를 묶습니다. ID가 없으면 `RTC_CANVAS_CONNECTION_REQUIRED`, ID 형식이 잘못되면 `RTC_CANVAS_CONNECTION_INVALID`, 해시가 없거나 일치하지 않으면 `RTC_CANVAS_CONNECTION_HASH_REQUIRED` 또는 `RTC_CANVAS_CONNECTION_HASH_INVALID`, 대상 소켓이 없으면 `RTC_CANVAS_SOCKET_REQUIRED`를 반환합니다. 기존 피어를 다시 `rtc_join`할 때도 같은 ID·해시 쌍을 보내야 하며, 다른 소켓으로 바꾸려 하면 `RTC_CANVAS_CONNECTION_MISMATCH`를 반환합니다.

클라이언트가 `{"type":"rtc_join","canvas_connection_id":"<init_items.rtc_canvas_connection_id>","canvas_connection_hash":"<init_items.rtc_canvas_connection_hash>"}`을 보내면 서버는 이 RTC 신호 연결 전용 `self_peer_id`와 같은 캔버스의 현재 피어 목록을 반환하고, 다른 같은 캔버스 피어에게 `rtc_peer_joined`를 보냅니다. 각 피어는 RTC 신호 연결 한 개를 나타냅니다. 이미 참여한 연결에서 `rtc_join` 또는 `rtc_list`를 보내면 현재 목록을 다시 받습니다.

```json
{"type":"rtc_ready"}
{"type":"rtc_peers","self_peer_id":"<ephemeral-peer-token>","peers":[{"peer_id":"<token>","nickname":"사용자","tag_number":12,"groups":["default"],"is_admin":false}]}
{"type":"rtc_peer_joined","peer":{"peer_id":"<token>","nickname":"사용자","tag_number":12,"groups":["default"],"is_admin":false}}
{"type":"rtc_peer_left","peer_id":"<token>"}
```

`rtc_disconnect`는 피어 등록만 해제합니다. RTC 신호 WebSocket은 열린 상태라 같은 연결에서 다시 `rtc_join`할 수 있습니다. 다른 피어는 `rtc_peer_left`를 받으며, 요청한 연결은 `rtc_disconnected`를 받습니다. RTC 신호 WebSocket 자체가 닫히면 그 연결의 피어가 제거됩니다. 캔버스 WebSocket이 닫히면 **그 캔버스 WebSocket에 묶인 피어만** 즉시 제거하고 해당 RTC 신호 WebSocket에 `{"type":"rtc_disconnected","reason":"canvas_socket_closed"}`를 보냅니다. RTC 신호 WebSocket 자체는 닫지 않습니다. 다른 캔버스 WebSocket에 묶인 피어는 유지됩니다. 캔버스 활성 상태와 SQL `user_sessions`는 캔버스 WebSocket만 기준으로 갱신합니다. 피어 해제 알림을 받은 뒤 실제 `RTCPeerConnection`과 데이터 채널을 닫는 것은 클라이언트의 책임입니다.

```json
{"type":"rtc_disconnect","reason":"failed"}
```

signaling 서버는 브라우저의 실제 ICE 연결 상태를 직접 관찰하지 않습니다. 클라이언트가 연결 종료를 알릴 때 이 이벤트를 사용합니다. 응답은 `{"type":"rtc_disconnected"}`입니다. 캔버스 이벤트용 WebSocket에서는 `rtc_*` 요청을 `RTC_SEPARATE_CHANNEL_REQUIRED`로 거절합니다.

RTC 신호 WebSocket은 `rtc_join`, `rtc_list`, `rtc_disconnect`, `rtc_signal` 이벤트를 처리합니다. `rtc_signal`은 WebRTC offer/answer와 ICE candidate를 전달합니다. 피어 목록과 대상은 캔버스 ID별로 분리되며, 다른 캔버스의 피어를 지정하면 `RTC_PEER_NOT_FOUND`를 반환합니다. 서버는 발신 피어 정보를 붙여 같은 캔버스의 활성 피어 한 곳에만 전달하며 Redis나 Elasticsearch에 저장하지 않습니다. offer/answer는 `type`과 `sdp`, candidate는 허용된 표준 필드만 전달합니다. `rtc_join` 전의 신호는 `RTC_NOT_JOINED`로 거절합니다.

```json
{"type":"rtc_signal","peer_id":"<target-peer-token>","action":"offer","description":{"type":"offer","sdp":"..."}}
{"type":"rtc_signal","peer_id":"<target-peer-token>","action":"candidate","candidate":{"candidate":"...","sdpMid":"0","sdpMLineIndex":0}}
```

실제 WebRTC 미디어와 데이터 채널 패킷은 C++ 서버를 경유하지 않습니다. C++는 SDP/ICE 신호만 전달하고 실제 패킷을 중계·저장·해석하지 않습니다. 실제 경로는 클라이언트 ICE 설정과 네트워크에 달려 있습니다. 클라이언트에서 TURN을 설정하면 별도 TURN 서버를 경유할 수 있습니다. 중계 없는 직접 연결만 허용하려면 TURN을 설정하지 않아야 하며, 일부 네트워크에서는 연결이 실패할 수 있습니다. RTC 신호 WebSocket은 JSON 이벤트만 받으며, 최대 메시지 크기는 256 KiB입니다. 연결당 초당 100개를 초과하는 메시지는 처리되지 않습니다.

#### 연결 상태 확인

```json
{"type":"ping"}
```

응답:

```json
{"type":"pong","canvas_id":1,"timestamp":1700000000}
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

가능한 실패 코드에는 `CANVAS_NOT_READY`, `SERVER_BUSY`, `SETTINGS_STORAGE_ERROR`, `SETTINGS_ACCESS_DENIED`, `SETTINGS_INVALID_INPUT`, `SETTINGS_REVISION_REQUIRED`, `SETTINGS_CONFLICT`, `SETTINGS_USER_NOT_FOUND`, `SETTINGS_ALREADY_PARTICIPANT`, `SETTINGS_OWNER_REQUIRED`가 있습니다. 저장소를 사용하는 C++ WebSocket 작업은 최대 8개까지 worker에서 처리하며, 자리가 없으면 `SERVER_BUSY`를 반환합니다. `SETTINGS_CONFLICT`에는 최신 `settings`가 함께 반환되므로 이를 반영해 새 revision으로 재시도해야 합니다.

### 종료·재접속

WebSocket close 시 C++ 서버가 사용자 세션을 비활성화합니다. Spring의 연결 종료 요청과 회원 삭제는 `is_accessed`·캔버스·C++ 서버 연결 필드를 직접 변경하지 않고 C++ WebSocket 종료 경로에 맡깁니다. 캔버스 row나 캔버스 문서는 소켓 close로 삭제하지 않습니다. 서버 종료 시에도 연결 종료와 세션 정리만 수행하며, 캔버스 삭제는 별도의 `DELETE /api/canvas/{canvasId}` 제어 API입니다. 설정 revision 변경으로 기존 토큰이 무효화되면 서버는 close code `1008`로 연결을 종료하고 새 `/access` 토큰을 요구합니다.

## 6. 시간 복잡도·저장소 접근·동시성

아래 복잡도는 현재 구현의 애플리케이션 측 알고리즘 기준입니다. 네트워크 왕복, SQL 실행 계획, Elasticsearch/Redis 내부 인덱스와 디스크 지연은 Big-O만으로 상한을 보장할 수 없습니다. `unordered_map`/`unordered_set` 조회는 평균 O(1), 해시 충돌이 극단적인 최악의 경우 O(n)입니다. 응답을 실제로 보내거나 직렬화하는 비용은 전송 바이트 수에 비례합니다.

### 변수 정의

| 기호 | 의미 |
|---|---|
| `B` | 요청 JSON의 바이트 수 |
| `B_doc` | Redis/ES Canvas JSON 문서의 바이트 수 |
| `B_out` | 모든 수신 소켓에 직렬화·전송할 응답 바이트 총량 |
| `C` | C++ 프로세스의 활성 Canvas 객체 수 |
| `S` | 해당 캔버스에 연결된 캔버스 이벤트용 WebSocket 수 (한 사용자에게 여러 소켓이 있을 수 있음) |
| `S_sig` | 해당 캔버스에 열린 RTC 신호 WebSocket 수 (피어 등록 전·해제 후 연결도 포함) |
| `S_rtc` | 해당 캔버스에 등록된 RTC 신호 피어 수 |
| `K_rtc` | 캔버스 WebSocket 하나에 묶여 있어 그 연결 종료 시 해제할 RTC 피어 수 |
| `R` | 이벤트를 받을 권한이 있는 소켓 수 (`R ≤ S`) |
| `U` | 해당 캔버스의 활성 사용자 수 |
| `P` | 캔버스 `people` 참여자 수 |
| `E_g` | `inner-group` 전체 사용자-그룹 연결 수 |
| `G` | 한 사용자가 가진 그룹 수 |
| `A` | 아이템 또는 채팅방 ACL에 적힌 그룹 수 |
| `N` | 캔버스 아이템 수 또는 bulk 요청의 아이템 수 (문맥에 따라 표시) |
| `H` | 초기 Canvas load에서는 모든 방의 저장 메시지 수 합, history 조회에서는 요청한 방의 메시지 수 |
| `L` | 채팅 내역 응답 페이지 크기 (최대 200) |
| `Q` | 해당 캔버스에 앞서 쌓여 현재 기다리는 저장 이벤트 수 |
| `V` | Spring에 등록된 C++ 서버 수 또는 Redis 서버 수 |
| `D` | SQL 테이블의 행 수 |
| `F` | 대표 이미지 등 파일의 바이트 수 |
| `T` | Spring 캔버스 검색이 반환하는 문서 수 |
| `W` | C++에서 설정·내역·접속 검증에 사용하는 동시 blocking worker 수 (최대 8) |
| `K` | ACL 그룹 수신자 색인을 조회할 때 각 ACL 그룹에 속한 소켓 수의 합 (중복 소켓은 각 그룹마다 합산) |
| `R_acl` | 해당 ACL 그룹 색인에서 합쳐진 고유 후보 소켓 수 |

### Spring Boot API

| API 묶음 | 애플리케이션 측 비용과 저장소 접근 |
|---|---|
| 로그인, 사용자 단건 조회/수정/삭제, 내 정보 | 사용자 키 조회와 응답 조립은 인덱스 조회가 정상 동작한다는 전제에서 O(1)개의 행 처리. 닉네임 변경은 새 닉네임의 잠금된 최대 tag 조회를 추가합니다. 비밀번호 해시 검증/생성은 설정된 해시 비용만큼 매 요청 수행합니다. 사용자 전체 목록은 O(D). |
| 회원가입 | 고정 개수의 사용자/session SQL 조회·쓰기 + 비밀번호 해시. 닉네임 tag 발급은 트랜잭션 안에서 `UPDLOCK, HOLDLOCK`을 건 `MAX(tag_number)` 쿼리입니다. |
| 캔버스 생성 | 고정 개수의 SQL/Elasticsearch 쓰기와 파일 저장 O(F). |
| 캔버스 목록·검색 | Elasticsearch 검색 결과 `T`건을 응답으로 변환하므로 O(T + 응답 바이트). 이름 생략 검색은 `match_all` 결과를 최대 1000건 요청합니다. Elasticsearch 검색 자체의 비용은 인덱스/샤드/쿼리 실행 계획에 따릅니다. |
| 캔버스 단건 조회 | Elasticsearch `_doc/{id}` 조회 1회와 실패 시 `term` search fallback 1회까지, 고정 크기 응답 조립. |
| 설정 조회 | SQL `canvas_info` 비관적 쓰기 잠금 1회, 활성은 Redis 문서 1회, 비활성은 Elasticsearch `_doc/{id}` 조회와 실패 시 `term` search fallback까지, 참여자 `findAllById` 일괄 SQL 조회 1회. 문서 read/parse O(B_doc), 결과 조립 O(P). 현재 트랜잭션은 외부 저장소 응답을 기다리는 동안 canvas row 잠금을 유지합니다. |
| 이름/설명/비밀번호 변경 | 비활성 여부를 위한 canvas row 잠금, Elasticsearch 문서 읽기(직접 ID 조회 실패 시 search fallback)와 patch. 참여자 배열 복사는 없으며 일반 필드는 O(1) 앱 연산입니다. 비밀번호는 해시 비용이 추가됩니다. 활성 캔버스면 Elasticsearch 접근 전에 `409 CANVAS_006`으로 끝납니다. |
| 참여자 추가/제거 | 비활성 확인 및 문서 읽기 후 사람·그룹 배열을 복사/탐색하므로 O(P + E_g), 사용자 식별 SQL 조회와 Elasticsearch patch가 있습니다. 활성 캔버스면 ES 문서 읽기 전에 409로 거부합니다. |
| 캔버스 삭제 | SQL 잠금·접속 세션 확인·삭제, ES 문서 읽기/삭제, 파일 정리 O(F). 활성 또는 접속 세션이 있으면 삭제하지 않습니다. |
| `/access` | canvas row 잠금 + 활성 Redis 문서 또는 비활성 ES 문서 읽기/parse O(B_doc) + 보호된 캔버스의 비밀번호 해시 검사. Spring은 `people` 참여 검사를 하지 않습니다. 새 서버 배정이면 활성 서버 `V`개를 순회해 health check하고, 2개 서버를 고른 뒤 C++ 부하 조회를 최대 2회 수행합니다. 네트워크 지연이 앱 연산보다 지배적입니다. |
| `GET/POST /api/load-balancer/allocate/server`, `GET/POST /api/load-balancer/allocate/redis` | 등록 후보 `V`개를 순회해 health check 또는 DB 부하를 확인합니다. 서버 배정은 최대 V번의 health check와 선택 후 C++ 부하 조회 2회, Redis 배정은 후보 조회와 선택 후 SQL 부하 집계 2회입니다. |
| `/api/auth/health`, `/api/load-balancer/database`, `/api/database/address` | 설정값/상태 응답 생성 O(1), DB/ES 조회 없음. |
| 테스트 프록시 | 서버/DB 조회 결과 수에 비례하며 C++ 제어/health/socket API 호출의 네트워크 시간이 추가됩니다. |

JPA의 단건 키 조회가 실제로 O(1)인지 O(log D)인지는 스키마 인덱스와 DB 실행 계획에 달려 있어 코드만으로 보장할 수 없습니다. 목록·검색 응답은 결과가 커지는 만큼 선형으로 증가합니다.

### C++ HTTP 제어 API

| API | 애플리케이션 측 비용 |
|---|---|
| `/health`, `/` | O(1). 외부 저장소 조회 없음. |
| `GET /api/canvas/count` | 현재 구현은 C개 캔버스를 순회하고 매 캔버스의 활성 사용자 `set`을 복사하므로 O(C + ΣU). |
| `GET /api/canvas/active` | 캔버스 ID와 사용자 목록을 순회/복사하므로 O(C + ΣU), 응답 크기만큼 직렬화합니다. |
| `POST /api/users/{id}/disconnect` | C개 캔버스를 검색하고 일치하는 캔버스의 이벤트·RTC 신호 소켓을 닫습니다. O(C + 해당 캔버스들의 S + S_sig 합), 피어 퇴장 알림 전송 비용은 별도입니다. |
| `POST /api/canvas/{id}/users/{id}/disconnect` | Canvas map 조회 평균 O(1), 그 캔버스의 이벤트·RTC 신호 소켓을 찾아 닫는 단계 O(S + S_sig), 피어 퇴장 알림 전송 비용은 별도입니다. |
| `DELETE /api/canvas/{id}` | map/lifecycle lock 조회 평균 O(1), 활성 사용자 검사 O(U)와 SQL 세션 조회. 활성 세션이 있으면 `removed:false`를 반환합니다. 비활성 시 대기 저장 큐 처리 O(Q), Redis 문서 읽기/ES 저장/Redis 정리와 SQL 갱신. 문서 및 채팅 내역 바이트가 클수록 전송·직렬화 비용이 커집니다. |

### C++ 연결 및 상시 처리 경로

1. **접속 전 검사**: WebSocket upgrade에서 JWT 서명·claim 확인과 MSSQL 닉네임/tag 조회를 최대 8개 동시 작업으로 제한한 worker에서 수행합니다. 그 다음 open worker가 Canvas 할당과 참여자 `people`/`settings_revision`을 확인합니다(O(P)); 활성 Canvas에서는 Redis, 비활성 Canvas에서는 Elasticsearch 문서를 읽습니다. 이 검사는 Canvas 캐시 할당과 세션 예약 전에 끝납니다. worker 포화 시 HTTP 503 또는 WebSocket close 1013으로 접속을 거절합니다.
2. **초기 로드/세션 예약**: 첫 접속은 MSSQL 할당 transaction과 Redis 연결/문서 로드가 추가됩니다. 비활성 Canvas 문서가 Redis에 없으면 preflight에서 ES를 읽고 Canvas 초기화 중 같은 문서를 다시 ES에서 읽어 Redis에 적재합니다. 문서 크기 `B_doc`에 비례하는 중복 read가 첫 접속 지연에 포함됩니다. 문서 parse/그룹 연결/아이템 ACL/채팅방 색인과 초기 snapshot 필터링은 O(B_doc + E_g + N·A + H)까지 들 수 있으며 모두 worker에서 수행합니다. 사용자 세션 예약은 MSSQL transaction에서 Canvas row와 사용자 session row를 잠급니다. 활성 Canvas의 후속 접속은 권한 preflight와 최종 revision 확인에서 최신 Redis 전체 문서를 각각 읽고 파싱하므로 연결마다 O(B_doc) 문서 처리와 Redis 왕복 2회가 있습니다. 최종 Redis read/parse와 초기 snapshot 작성은 worker에서 끝내고, event loop에서는 메모리 색인을 옮기고 응답을 전송합니다. 최종 revision 확인은 참여 여부를 DB에서 다시 조회하지 않습니다.
3. **연결 시 메모리에 준비되는 정보**: `PerSocketData`에 `user_id`, `nickname`, `tag_number`, `settings_revision`, 사용자 그룹, 관리자 여부, 권한 완료 상태를 둡니다. Canvas별 색인에는 아이템 ACL, 채팅방 ID와 다음 sequence, 그룹→소켓과 관리자 소켓을 둡니다. 일반 아이템/채팅 이벤트마다 사용자 정보를 DB에서 다시 조회하지 않습니다. 색인 생성은 O(G)이며 설정 스냅샷 표시명만 별도 SQL 작업입니다.
4. **접속 해제**: socket/user map 삭제는 평균 O(1), 그룹 색인 삭제는 O(G)입니다. 소켓을 실제 닫는 비용은 해당 소켓 수에 비례합니다. 내부 `sendToUser`, session generation 갱신, 사용자별 disconnect는 현재 Canvas의 S개 소켓을 순회합니다. MSSQL 세션 해제는 generation 번호를 확인하는 비동기 작업입니다. 마지막 사용자 종료 후 Canvas unload는 전체 문서 저장이 필요합니다.

### WebSocket 메시지와 브로드캐스트

| 이벤트 | 권한 검사 시점 및 비용 | 브로드캐스트/저장 비용 |
|---|---|---|
| `ping` | 연결 때 검증된 소켓 상태만 확인, O(1). | 바로 `pong`, O(1). DB/Redis/ES 접근 없음. |
| 일반 단건 아이템 변경/삭제 | 연결 때 준비한 사용자 그룹과 Canvas 메모리 ACL로 검사합니다. ACL 교집합 검사는 평균 O(min(A,G)); hash 조회는 평균 O(1)입니다. | ACL 그룹→소켓 색인에서 후보를 가져와 `O(A + K + R_acl·min(A,G))` 평균 비용입니다. 최악에는 한 그룹에 S개가 모두 있어 O(S·A)까지 커집니다. Redis 저장은 캔버스별 FIFO worker로 큐잉하며 broadcast 콜백 안에서는 저장소를 호출하지 않습니다. |
| 일반 비아이템 broadcast | 연결 승인된 소켓 상태 확인 O(1). | O(S) 소켓 순회 + 수신자별 `B_out` 전송. |
| `chat` | 메모리에서 방 존재/ACL을 확인합니다. sender ACL 교차 검사는 평균 O(min(A,G)); 방별 sequence/map 조회는 평균 O(1). | ACL 그룹 색인을 사용해 `O(A + K + R)` 평균 비용으로 후보를 모으고 전송합니다. 그룹 색인 조회에는 중복 제거용 메모리가 O(R) 필요합니다. 수신자에게 보내는 비용은 적어도 O(R)입니다. 메시지/sequence를 FIFO persistence queue에 넣은 뒤 Redis 저장 완료를 기다리지 않고 전송합니다. Redis Lua append는 background worker가 수행합니다. 이 hot path에서 SQL/Elasticsearch 호출은 없습니다. |
| RTC 신호 연결 | JWT를 접속 시 확인합니다. Canvas 로드·SQL 세션 예약은 하지 않습니다. | `rtc_join`은 권한이 확인된 같은 사용자·캔버스의 WebSocket 색인을 평균 O(1)로 확인합니다. 신호 연결만으로 캔버스가 활성 상태가 되지는 않습니다. |
| `rtc_join` / `rtc_disconnect` | 피어 map과 캔버스 WebSocket 연결 ID 색인의 추가·제거는 평균 O(1). | 다른 RTC 피어 `S_rtc`명에게 입장·퇴장 이벤트를 전달하고, 입장 시 목록을 구성하므로 O(`S_rtc`)입니다. 캔버스 WebSocket 하나가 종료될 때는 해당 연결에 묶인 `K_rtc`개 피어만 색인에서 찾아 해제하므로 O(`K_rtc`·`S_rtc`)입니다. |
| `rtc_signal` | peer ID 메모리 map 조회 평균 O(1). SDP/candidate 검증은 payload 바이트 B에 비례합니다. | 단일 대상 RTC 신호 소켓에 전달, 직렬화/전송은 O(B). SQL·Redis·Elasticsearch 호출이나 미디어 중계는 없습니다. |
| bulk `items` 교체 | 관리자 확인은 O(1). 새 아이템 ACL과 기존 ACL 색인을 순회하므로 O(N·A). | 새/기존 ACL 그룹 수신자 색인에서 후보를 모은 뒤 각 후보 소켓에 N개 아이템을 필터링하고 이전 권한 회수를 검사합니다. 평균 앱 비용은 `O(N·A + K + R_acl·N·A)` 이하이며, 그룹이 모든 소켓을 포함하면 O(S·N·A)까지 커집니다. 받는 소켓별 JSON 생성/전송 바이트 비용이 추가됩니다. 저장은 background Redis worker입니다. |
| `chat_history` | 요청 시점에 메모리 방 ACL을 검사합니다(O(A)). 응답 worker 완료 시 ACL을 다시 훑지 않고 Canvas 권한 epoch가 바뀌지 않았는지 O(1) 평균 확인합니다. | 최대 W개 worker 중 하나에서 앞선 저장 큐를 기다리고 Redis를 조회합니다. Q개 pending write가 끝나야 하므로 대기 시간은 Q와 저장소 지연에 좌우됩니다. 같은 event loop의 다른 실시간 이벤트는 Redis 응답을 기다리지 않습니다. 응답 페이지는 최대 L개이며, RedisJSON range 연산 비용은 Redis 구현에 따릅니다. legacy/non-contiguous 내역 fallback은 H개 메시지를 읽고 정렬해 O(H log H), 응답 O(L)입니다. Elasticsearch/SQL은 사용하지 않습니다. |
| `canvas_settings_get` / `canvas_settings_update` | worker에서 Redis 문서를 읽고 파싱 O(B_doc), 참여 여부 O(P), 관리자 그룹 확인 O(E_g)을 수행합니다. 설정 요청마다 MSSQL Canvas 할당과 활성 사용자 확인을 각각 수행합니다. | 저장소 작업은 최대 W개 worker에서 실행합니다. 응답 표시명은 현재 참여자마다 `getUserHandle` SQL을 요청하므로 O(P) round trip이며, `participant_remove`도 대상 ID 검색에 최대 P회 SQL을 호출합니다. 새 참여자 추가는 SQL 조회 1회입니다. 비밀번호 설정은 해시 비용이 추가됩니다. 업데이트는 Redis revision CAS 후 ES patch를 수행하고, 같은 Canvas의 설정 변경/최종 unload 저장은 `settings_mutex`로 직렬화합니다. participant-remove pending 표시 및 변경/퇴장 알림으로 event loop에 O(S) 순회가 남습니다. 제거 중 대상 소켓의 요청은 임시 거부하고, 실패 시 다시 허용합니다. |

`participant_remove`는 event loop에서 대상 socket을 pending 상태로 바꾸고 완료 알림/퇴장을 적용하므로 요청 시작과 완료에 각각 O(S) 순회가 있습니다. 저장소 작업은 worker에서 수행합니다.

브로드캐스트는 R명에게 실제 전송해야 하므로 O(1)은 불가능하며 하한도 Ω(R)입니다. 단건 chat/item은 ACL 그룹→소켓 색인으로 무관한 소켓 순회를 줄였습니다. 이 색인은 접속/해제에 O(G) 비용과 O(S·G) 메모리를 쓰며, ACL 그룹이 전체 소켓을 포함하면 팬아웃도 O(S)입니다. 일반 공개 broadcast는 여전히 O(S)이고, bulk 교체는 최대 O(S·N·A)입니다. fan-out과 실제 WebSocket 전송은 event loop에 남아 있어 수신자가 많거나 bulk payload가 크면 loop 점유 시간이 늘어납니다. 저장소 대기는 loop에서 분리했습니다.

### Elasticsearch 접근 경로 확인

- **C++ 실시간 일반 아이템/채팅 브로드캐스트**: Elasticsearch 호출 없음. ACL은 접속 시 메모리에 준비한 사용자 그룹·아이템 ACL로 판정하고, 영속화는 Redis worker로 보냅니다. `ping`, `chat_history`도 ES를 사용하지 않습니다.
- **C++ Canvas load**: 비활성 Canvas의 권한 preflight/document load, 최초 Canvas 초기화 또는 legacy password migration에서 ES를 읽거나 저장합니다. 활성 Redis 문서가 없거나 잘못됐을 때 ES로 fallback하지 않고 fail-closed 합니다.
- **C++ Canvas save/unload 및 활성 설정 저장**: 마지막 접속자가 나갈 때 Redis의 최종 문서를 ES에 저장합니다. `canvas_settings_update`는 활성 설정을 Redis CAS 후 ES patch하여 성공 응답 전에 반영합니다. ES 실패 시 Redis/ES 보상 복구를 시도합니다. 이는 명시적인 설정 저장 동작이며 broadcast 경로는 아닙니다.
- **Spring Boot**: 캔버스 생성/수정/삭제 저장 외에도 캔버스 검색·목록·단건 요약, 비활성 설정 조회에서 ES를 조회합니다. 활성 설정/access 조회는 Redis 문서를 읽습니다. 따라서 “load/save 외 ES 접근 금지”를 C++ hot path에 한정하면 만족하지만, Spring의 캔버스 조회/검색 API까지 포함하는 전역 규칙으로 읽으면 현재 구현은 그 규칙을 만족하지 않습니다.

### 동시성·지연 한계

- MSSQL session reserve는 transaction 안에서 Canvas row와 `user_sessions` row에 `UPDLOCK, HOLDLOCK, ROWLOCK`을 사용합니다. C++는 사용자별 mutex와 session generation으로 오래된 disconnect가 새 연결 상태를 지우지 않게 합니다.
- 회원가입 및 닉네임 변경의 `MAX(tag_number)+1`도 transaction 내 `UPDLOCK, HOLDLOCK` 쿼리로 같은 닉네임의 동시 배정을 직렬화합니다. 이름이 없는 경우까지 포함해 SQL Server가 serializable range lock을 유지합니다. `users.nickname` 인덱스가 없으면 잠금 범위가 넓어져 서로 다른 닉네임 작업도 경합할 수 있습니다.
- C++ Canvas load/unload는 Canvas lifecycle mutex로 직렬화합니다. 설정 변경은 `settings_mutex`와 Redis revision compare-and-set으로 직렬화/충돌 감지를 하며, item/chat 저장은 Canvas별 FIFO queue와 chat append Lua로 순서를 보존합니다.
- Redis 저장 실패는 해당 Canvas의 persistence 상태에 기록됩니다. 이후 item/chat 저장 요청을 거절하고 Redis→Elasticsearch 스냅샷 및 캐시 해제를 중단합니다. 캐시를 유지한 채 운영자가 원인을 확인해야 하며, 이미 실시간 전달된 이벤트가 영속 저장되었다고 간주해서는 안 됩니다.
- 언로드는 Elasticsearch 저장 후 MSSQL의 `is_cached`를 먼저 해제하고, 이전 Canvas 로드에 부여한 `_cache_generation`이 일치할 때만 Redis 키를 삭제합니다. Spring의 canvas row 잠금과 이 순서로 활성 Redis 조회 중 키가 먼저 사라지는 문제를 피합니다. 새 로드는 비활성 할당에 남은 Redis 키를 무시하고 Elasticsearch 문서로 덮어씁니다.
- 삭제 요청과 자동 언로드는 활성 캔버스 WebSocket 사용자와 SQL 세션이 모두 없을 때만 진행합니다. RTC 신호 연결만 남아 있어도 언로드할 수 있으며, 캔버스 WebSocket이 닫힐 때마다 그 사용자의 RTC 피어 등록은 해제됩니다. 언로드 과정에서 나중에 실행될 소켓 전체 종료 작업을 예약하지 않습니다.
- 접속 해제 시 MSSQL 세션 갱신은 단일 전용 worker에 사용자별 최신 작업을 모아 처리합니다. 접속 해제 횟수에 비례해 스레드가 늘어나지 않습니다. 활성 세션 수 SQL 결과를 읽는 데 실패하면 캔버스를 활성 상태로 간주해 언로드를 미룹니다.
- 명시적 캔버스 언로드가 모든 소켓을 닫으면서 Canvas의 활성 사용자 목록을 먼저 비운 경우에도, 각 소켓 종료 콜백이 MSSQL 세션 해제를 예약합니다.
- Canvas persistence queue는 `settings_mutex`와 별도 mutex를 사용합니다. event loop의 enqueue는 짧은 queue lock만 얻으며, worker의 SQL/Redis/ES 작업이나 history/connect 대기와 경쟁하지 않습니다. 각 저장 이벤트에 증가하는 ticket을 붙여 history/connect는 요청 전에 접수된 write까지만 기다리고, unload는 종료를 표시한 뒤 마지막 ticket까지 기다립니다.
- 이 잠금들은 선형 작업량을 O(1)로 바꾸지 않으며, 대기 시간은 lock hold 및 저장소 round trip에 좌우됩니다. Spring `/access`는 canvas row 잠금을 네트워크 health check 동안 유지합니다. C++ upgrade 인증, 접속 snapshot, `chat_history`, `canvas_settings_*`의 저장소 호출은 worker로 분리했습니다. 설정 worker는 동일 Canvas의 설정/unload와 직렬화되며, 최대 W개 worker가 모두 사용 중이면 새 작업을 거절합니다. 실시간 broadcast fan-out과 socket send는 event loop에 남아 있어 S/R/N이 커질수록 지연이 증가할 수 있습니다.
- item/chat 이벤트는 Redis 저장 완료를 기다리지 않고 queue 후 전송을 진행합니다. 따라서 delivery latency는 낮지만, broadcast 시점은 Redis durable write 완료를 뜻하지 않습니다. 연결/초기 snapshot과 history는 필요한 경우 queue barrier를 기다립니다.

## 7. 상태·운영 참고

- C++는 시작 시 `cpp_server`를 등록/활성화하고 5초마다 `last_heartbeat_at`을 갱신합니다. 정상 종료 시 row를 삭제하지 않고 비활성화합니다.
- Spring은 서버 선택 시 15초 이내 heartbeat와 C++ `/health` 응답을 모두 확인합니다.
- WebSocket 캔버스 세션은 Redis의 최신 문서를 기준으로 초기화하고, 마지막 접속자가 나가면 캐시를 해제하고 영속 저장 흐름을 수행합니다.
- `people`는 참여·접속 권한의 내부 목록입니다. Spring은 로그인 및 비밀번호를 확인한 뒤 설정 revision을 포함한 토큰을 발급합니다. C++는 캐시 할당과 세션 예약 전에 Redis 또는 Elasticsearch에서 참여자와 revision을 한 번 확인하고, 로드 후에는 동시 설정 변경 여부를 revision으로만 확인합니다. 활성 캐시의 Redis 문서를 읽을 수 없으면 Elasticsearch의 오래된 사본으로 대체하지 않고 접속을 거부합니다.
