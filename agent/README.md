# 캔버스 AI 에이전트

OpenAI(기본)·Anthropic Claude·Google Gemini·OpenRouter 중 사용자가 고른 모델 API를 호출하고, 공식 MCP Python SDK의 서버·클라이언트 세션을 통해 캔버스를 읽고 수정한다. Claude는 공식 Anthropic Python SDK로, 나머지는 OpenAI Python SDK(각 제공 서비스의 OpenAI 호환 API)로 호출한다.

## 설정

```sh
cd /home/steam/Github/project-agora-BE
cp -n agent/.env.example agent/.env
chmod 600 agent/.env
# 캔버스 에이전트를 활성화하려면 agent/.env에 운영자 AGENT_ANTHROPIC_API_KEY를 설정

docker compose -p agora-backend -f docker-compose.backend.yml up -d --build agent cpp
```

키를 바꾸면 `docker compose -p agora-backend -f docker-compose.backend.yml up -d --force-recreate --no-deps agent`로 환경 변수를 다시 읽힌다. 키가 비어 있으면 상태 확인과 이전 대화 조회만 가능하다. `.env`는 Git과 Docker 빌드 컨텍스트에서 제외한다.

캔버스 상단의 **AI 에이전트**에서 “파이썬 이진 탐색 코드 객체를 만들어줘”처럼 새 객체 생성을 요청할 수 있다.

## 경로와 권한

```
브라우저 WebSocket → Nginx → Phoenix 브로커 → C++
                                             ↕ Unix socket
                                Python agent ↔ MCP 서버/클라이언트
                                      ↕ HTTPS
                     Anthropic 상용 API
```

- 브로커는 인증된 현재 캔버스의 읽기 권한을 확인한다. 클라이언트의 사용자·캔버스 신원 필드는 사용하지 않는다.
- MCP 도구는 현재 요청에 바인딩된 C++ 콜백만 호출한다. 다른 캔버스 ID, SQL, 셸, 권한 변경 도구는 없다.
- C++는 생성 도구 호출 시 신원 만료·캔버스 설정 리비전·쓰기 권한을 다시 확인한다.
- 생성은 기존 `canvas_ops`의 원자적 저장과 ACK 경로를 이용하고, 커밋은 객체별 권한에 따라 전파한다.
- 모델 응답은 텍스트로만 표시한다. 모델이 출력한 문자열을 실행하거나 브로드캐스트 명령으로 해석하지 않는다. UI의 진행 알림과 실제 변경 패킷은 C++가 별도로 감싼다.
- 기본 모델 턴은 최대 8회·210초이며 동시 요청은 2개다.
- MCP는 프로세스 내부 메모리 스트림이다. 공개 MCP HTTP 포트가 없고 C++와 Python만 공유하는 UID 10001의 소켓(0660)을 사용한다. Python 컨테이너만 별도 egress 네트워크로 모델 API에 접속한다. 공개 포트는 추가하지 않는다.
- 모델 추론 내용은 사용자 화면에 스트리밍하지 않는다.
- 여러 도구 호출로 이루어진 작업 전체는 하나의 트랜잭션이 아니다. 시간 초과나 후속 오류가 나도 앞서 저장된 변경은 유지된다. 모델 API의 일시적 5xx는 해당 HTTP 요청만 최대 2회 재시도한다. 이미 수행한 MCP 도구나 작업 전체를 재실행하지 않으므로 생성 객체를 중복 만들지 않는다.
- 기존 대화는 캔버스에서 조회할 수 있다.

## 도구

| MCP 도구 | 동작 |
| --- | --- |
| list_objects | 권한이 있는 객체 요약 검색·페이지 조회 |
| get_objects | ID별 현재 원문과 리비전 조회 |
| create_objects | 사용자 그룹으로 객체 생성 |
| patch_objects | 원문 읽기 리비전을 비교하여 필요한 필드 수정 |
| delete_objects | 삭제 허용 옵션과 쓰기·잠금·리비전 검사 후 삭제 |

실행 환경/VM이나 코드 실행 권한은 에이전트의 코드 **작성**과 별개다. 에이전트가 작성한 코드는 사용자가 기존 실행 UI에서 실행한다.

## 검증

배포 런타임은 Python 3.13이다.

```sh
cd agent
python3.13 -m venv .venv
.venv/bin/pip install -r requirements.txt pytest pytest-asyncio
.venv/bin/python -m pytest tests
```

실제 MCP 초기화·도구 목록·읽기/수정/생성·충돌 전달·삭제 opt-in과 Unix 소켓 양방향 프로토콜을 모의 모델로 검증한다. 실제 제공 서비스 응답 검증에는 유효한 API 키가 필요하다.

참조: [MCP Python SDK](https://github.com/modelcontextprotocol/python-sdk), [OpenAI Python SDK](https://github.com/openai/openai-python), [Anthropic Python SDK](https://github.com/anthropics/anthropic-sdk-python).

실제 제공 서비스와 배포된 Unix 소켓을 이용한 수동 smoke test는 다음과 같다. MCP 도구의 저장 대상만 격리된 메모리 fixture로 대체하므로 실제 캔버스 데이터는 변경하지 않는다. 유효한 키가 필요하고 모델 API 사용량이 발생한다.

```sh
docker exec -i agora-agent python - --live < agent/tests/live_smoke.py
```

제공 서비스가 계속 혼잡하면 `AGENT_PROVIDER_BUSY`, 키/모델 인증 거부는 `AGENT_PROVIDER_AUTH_FAILED`, 제공 서비스 HTTP 오류는 `AGENT_PROVIDER_ERROR`로 구분한다. 키나 제공 서비스의 원본 오류 본문은 사용자 화면에 반환하지 않는다.

모델 API가 429를 반환하면 자동 재시도하지 않고 `AGENT_PROVIDER_BUSY`와 “요청이 너무 많습니다. 잠시 후 다시 시도해주세요.”를 즉시 반환한다.

## 캔버스에서 조절하는 설정과 호출 한도

AI 에이전트 설정에서 허용된 Claude 모델, 추론 수준, 응답 토큰 상한 및 최대 작업 단계를 조절할 수 있다. 한 작업 단계마다 모델 API를 호출할 수 있으므로 한 사용자 요청이 여러 API 호출을 소비할 수 있다.

호출 한도 카드는 EXAONE의 `x-{second|minute|day}-rate-limit` 헤더용이었으며 EXAONE 제거와 함께 화면에서 뺐다. 아래 수집 로직은 남아 있지만 현재 제공 서비스는 이 헤더를 보내지 않는다. 성공 및 429 오류 응답에서 `x-{second|minute|day}-rate-limit`와 `-remaining`만 추출한다. `retry-after`와 `retry-after-ms`가 있으면 해당 대기 시간도 표시한다. 수치는 **마지막으로 헤더를 받은 시점**의 값이며, 동일 키를 쓰는 모든 캔버스가 공유한다. 헤더가 없는 응답은 이전 한도와 그 확인 시간을 유지한다. 값이 없으면 “첫 응답 후 표시”로 안내하고 임의의 한도를 만들어내지 않는다.

허용된 숫자 메타데이터만 Unix 소켓 볼륨의 `quota.json`에 원자적으로 저장해 재시작 후 복구한다. API 키를 변경하면 이전 키의 한도는 재사용하지 않는다. 상태 확인 및 한도 새로고침은 저장된 메타데이터를 조회하며 모델 API를 추가 호출하지 않는다. 원본 헤더·키·쿠키는 클라이언트에 전달하지 않는다.

전용 AI 버튼은 캔버스 상단 중앙에 배치한다. 에이전트 창을 닫아도 진행 중 작업은 계속되며, 버튼의 작업 표시로 확인하고 다시 열 수 있다. 동작 줄이기 설정을 존중하고 유휴 상태의 반복 애니메이션은 사용하지 않는다.


## 모델 제공 서비스

캔버스 에이전트는 Anthropic 상용 API를 호출한다. 운영자 키는 `AGENT_ANTHROPIC_API_KEY`로 설정한다. 웹소켓 상태 응답은 키 설정 여부를 반환한다. 기본 모델은 Claude Opus 5.5이고 운영 허용 목록은 Opus 5.5, Sonnet 5.5, Haiku 5.5다.

호출에는 현재 데이터 전송 동의 버전이 필요하다. 모델에는 요청 본문과 새 객체 배치 위치만 보내며, 요청 수행 중 생성 도구 결과를 받을 수 있다. 모델에 제공하는 도구는 새 객체 생성으로 제한한다. 그래프 요청의 연결 입력과 이전 작업 결과가 포함된 본문은 전송 전 사용자가 확인한다.

운영 활성화 전에 [Anthropic 지원 지역](https://www.anthropic.com/supported-countries), 상용 계정의 데이터 처리 조건 및 실제 서비스 이용자의 국외 이전 근거를 확인한다.

## 캔버스 공유 대화 세션

대화는 브라우저 메모리 대신 캔버스 JSON의 `agent-sessions`에 저장한다. 내부에는 `drafts`(사용자별 빈 대화 포인터), `summaries`, `messages`, `receipts`(턴 ID 중복 실행 방지), `index`, `revision`이 있다. Redis Lua가 첫 요청 저장·사용자 메시지 추가·다음 빈 대화 생성을 원자적으로 처리한다. 새 대화 버튼을 여러 번 눌러도 동일 사용자의 빈 대화는 하나다. 첫 요청 후 현재 대화는 그대로 열어두고 다음 빈 대화를 준비한다.

사용자 메시지와 최종 응답/오류를 서버가 기록한다. 429 등 실패 시 입력창도 원래 입력문을 복원한다. **다시 사용**은 이전 사용자 메시지를 입력창에 복사하며 자동 실행하지 않는다. 다른 사용자의 대화는 읽기 전용이며 다시 사용하면 자신의 빈 대화로 가져온다. 실행 이력에는 API 키, 개인 한도, 추론 원문을 저장하지 않는다.

같은 캔버스 참가자에게 사용된 대화를 공유하되 저장된 접근 조건을 적용한다. 빈 대화는 작성자에게만 보이며, 작성자만 요청을 추가할 수 있다. 각 실행에는 현재 요청만 사용한다.

변경 저장 후 브로커가 내용 없는 `agent_sessions_changed` 알림을 전파한다. 클라이언트는 권한을 다시 확인하는 조회로 목록/선택 대화를 갱신한다. 알림 누락·재연결 복구를 위해 창이 열린 동안 15초 간격으로 메타데이터를 재확인한다(모델 호출 없음). 목록과 메시지는 페이지로 가져오며 전체 캔버스 객체를 읽지 않는다.

세션도 기존 캔버스 스냅샷 저장/복구 경로를 따른다. Elasticsearch에서는 매핑 필드 폭증을 막기 위해 `agent-sessions-b64` 청크로 보존하고, 로드 시 원래 JSON으로 복원한다. SQL 테이블을 추가하지 않는다. 5분 이상 종료 응답이 없는 대화는 연결 중단으로 표시하고, 같은 턴 ID를 재전송해도 모델 작업을 중복 실행하지 않는다. 이미 반영된 객체 변경은 오류/연결 중단으로 취소되지 않는다.

실제 Redis JSON 검증: `python3 cpp/tests/agent_sessions_redis_integration.py`(BE 루트). 브라우저 검증: FE의 `tests/integration/agent-sessions.mjs`.


## 저장 오류와 재연결 복구

대화 저장 Lua는 애플리케이션 Redis ACL에서 허용하는 `JSON.GET`, `JSON.SET`, `JSON.ARRLEN`, `JSON.ARRAPPEND` 등을 사용한다. Lua 실행 중 권한 오류가 나도 앞서 수행한 쓰기는 롤백되지 않으므로, 관리자가 아닌 실제 앱 계정으로 통합 테스트한다. 기존 버전에서 인덱스 기록 전에 저장이 중단된 대화는 버전 2 마이그레이션이 프롬프트를 보존하고 저장 오류로 종료 처리한다. 모델 작업을 자동 재실행하지 않는다. 객체 목록 조회에는 읽기 전용 `JSON.OBJKEYS` 권한이 필요하다.

클라이언트는 연결 종료·응답 시간 초과 때 같은 프롬프트를 다시 실행하지 않고 턴 ID로 저장된 결과를 조회한다. 재연결 알림과 주기 조회로 완료 결과를 복구하며, 확인할 수 없는 경우 입력문을 보존하고 사용자에게 안내한다. 마지막 대화 화면은 유지하고 변경 없는 조회 응답은 메시지 DOM을 교체하지 않는다. 자동 스크롤은 최신 위치를 보고 있을 때 새 메시지에만 적용한다.

FE 검증: `node tests/integration/agent-continuity.mjs`는 요청 중 연결 종료 후 결과 복구, 모델 중복 호출 방지, 기존 메시지 DOM 유지를 확인한다. `agent-sessions.mjs`는 오류 시 프롬프트 복원과 세션 공유를 검증한다.


대화 선택 목록의 삭제 버튼은 대화 작성자와 캔버스 관리자에게 표시된다. `agent/session_delete` 요청은 기존 캔버스 WebSocket을 통해 브로커와 C++로 전달되며 C++와 Redis Lua가 삭제 권한을 확인한다. 실행 중인 대화와 사용자별 빈 대화는 삭제할 수 없다. 삭제 시 요약·메시지·턴 중복 실행 기록·목록 인덱스를 함께 제거하고 세션 변경 알림을 전파한다. 캔버스 객체는 삭제하지 않는다. 현재 열어둔 대화가 삭제되면 새 대화로 이동한다.
