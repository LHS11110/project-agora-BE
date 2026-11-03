# Agora Canvas Python 모듈

Python 3.10 이상에서 기존 FreLog HTTPS API와 WSS 브로커에 접속합니다. DB나 내부 서버에 직접 접근하지 않습니다. TLS 검증은 항상 활성화됩니다.

```bash
pip install /path/to/project-agora-BE/sdk/python
```

## 객체를 생성하는 실행 예시

[`examples/create_objects.py`](examples/create_objects.py)는 기존 캔버스에 도형, 24px 텍스트, 14px 라벨이 있는 연결 화살표를 생성합니다. 종료 후 객체를 삭제하지 않습니다.

```bash
# SDK 설치 후, 해당 캔버스의 봇 토큰을 환경변수에 설정합니다.
export AGORA_BOT_TOKEN='발급받은 봇 토큰'
python examples/create_objects.py 2053  # 자신의 캔버스 ID로 변경
```

봇 대신 기존 사용자 토큰을 쓰려면 `AGORA_BOT_TOKEN`을 해제하고 `AGORA_USER_TOKEN`을 설정합니다. 둘 다 있으면 봇 토큰을 우선 사용합니다. 비밀번호가 있는 캔버스에는 `AGORA_CANVAS_PASSWORD`도 설정합니다. 캔버스 생성이나 봇 발급은 수행하지 않으며, 접속 계정에 읽기·쓰기 권한이 있어야 합니다. `permission`을 생략하므로 생성자의 모든 소속 그룹을 공유 대상으로 적용합니다.

## 기존 사용자로 접속

```python
import os
from agora_canvas import AgoraClient, MFARequired

with AgoraClient("https://frelog.p-e.kr") as client:
    try:
        client.login(os.environ["AGORA_EMAIL"], os.environ["AGORA_PASSWORD"])
    except MFARequired as challenge:
        client.complete_mfa(challenge.challenge, input("MFA 코드: "))
    # OAuth·패스키 로그인 등을 마친 기존 사용자 토큰도 사용 가능:
    # client = AgoraClient(token=os.environ["AGORA_USER_TOKEN"])
    with client.canvas(2053, password=None) as canvas:
        rectangle = canvas.create({
            "kind": "shape", "shapeType": "rectangle",
            "x": .2, "y": .2, "width": .15, "height": .12,
            "fill": "#ddd5ff", "color": "#6650b8", "permission": "default",
        })
        canvas.patch(rectangle, {"fill": "#b5e5d0", "strokeWidth": 2})
        canvas.move([rectangle], .01, 0)
        canvas.lock(rectangle)
        canvas.lock(rectangle, False)  # 잠금 해제는 별도 요청
        canvas.delete(rectangle)
```

익명 접속은 불가능합니다. 사용자/봇 모두 캔버스 읽기 권한이 필요하고, 생성·수정·삭제는 쓰기 권한, 실행은 실행 권한이 필요합니다. 객체별 그룹과 잠금도 기존 서버에서 검증합니다. 로그인 토큰은 기존 정책대로 IP에 묶이므로 같은 네트워크에서 발급받아 사용하세요. MFA를 우회하지 않습니다.

## 봇 참여자 생성과 접속

캔버스 소유자·관리자 그룹 사용자만 봇을 생성할 수 있습니다. 봇 자체는 다른 봇을 생성할 수 없습니다.

```python
import os
from agora_canvas import AgoraClient

with AgoraClient(token=os.environ["AGORA_USER_TOKEN"]) as owner:
    with owner.canvas(2053) as canvas:
        created = canvas.create_bot("문서 정리")
        # 전용 계정을 만든 뒤 기존 participant_add API로 초대합니다.
        # 생성 토큰은 이 응답에서만 공개되므로 비밀 저장소에 보관하세요.
        bot_token = created["token"]
        bot_id = created["bot"]["user_id"]

with AgoraClient.from_bot(bot_token) as bot:
    with bot.canvas(2053) as canvas:
        note = canvas.create({"kind":"note", "text":"자동 작성", "x":.4, "y":.3,
                              "width":.15, "height":.12, "permission":"default"})
        canvas.set_text(note, "변경된 내용")
        print(canvas.items[note])
```

봇은 생성된 캔버스에만 접근할 수 있고, 초대된 초기 그룹의 읽기·쓰기·실행 권한을 따릅니다. 비밀번호가 있는 캔버스에는 봇도 비밀번호가 필요합니다. 서버에는 256비트 비밀값의 SHA-256 해시만 저장하며 일반 계정 로그인은 금지됩니다. 메일은 실제 메일이 아닌 내부 서비스 계정 식별자입니다. 토큰을 객체·채팅·Git에 저장하지 마세요.

```python
owner.bots(2053)                       # 토큰 없는 봇 목록
rotated = owner.rotate_bot(2053, bot_id) # 새 토큰을 한 번 반환, 기존 토큰 폐기
with owner.canvas(2053) as canvas:
    canvas.revoke_bot(bot_id)           # 참여자 제거로 연결 종료 + 자격 증명 폐기
```

직접 `owner.revoke_bot(...)` HTTP 호출만 하면 새 HTTP 인증은 즉시 거부되며 이미 열린 WSS는 최대 60초의 접속 임대가 끝날 때 종료됩니다. 즉시 참여자 제거까지 하려면 `CanvasSession.revoke_bot()`을 쓰세요. 봇 접속 임대는 SDK가 자동 갱신합니다. 생성자 탈퇴·정지는 새 봇 인증도 차단합니다.

## 객체·연결·실시간 이벤트

`create()`/`patch()`는 기존 객체 필드를 그대로 사용하므로 도형, 텍스트, 코드, 드로잉, 이미지, 표, 그래프, 연결선 등 서버가 지원하는 객체를 처리합니다. 좌표는 기존 캔버스 문서와 동일한 정규화 좌표입니다. 필드 스키마는 BE `API_SPEC.md`와 FE `docs`를 참고하세요. 텍스트·메모·코드의 본문 교체는 `patch()` 또는 `set_text()`가 이전 Automerge 스냅샷을 제거하는 전체 교체로 전달합니다. 서버는 이후 브라우저 편집에서 새 협업 문서를 구성합니다. Python 전체 교체는 문자 단위 CRDT 병합이 아니므로 동시 편집 중에는 최근 원문을 확인하고 사용하세요. `code=True`이면 `code` 필드를 교체합니다.

```python
canvas.apply([
    {"action":"create", "id":"code-a", "patch":{
        "kind":"code", "code":"print(1)", "language":"python", "filename":"a.py",
        "x":.1,"y":.1,"width":.2,"height":.2,"permission":"default"}},
    {"action":"create", "id":"output-a", "patch":{
        "kind":"shape", "shapeType":"rectangle", "runtimeRole":"output",
        "x":.4,"y":.1,"width":.2,"height":.2,"permission":"default"}},
    {"action":"create", "id":"edge-a", "patch":{
        "kind":"connector", "from":"code-a", "to":"output-a", "permission":"default"}},
])
canvas.refresh()                        # 최신 서버 스냅샷
canvas.items                            # 백그라운드 수신으로 갱신되는 복사본
canvas.next_event(timeout=5)             # 변경 ID·이동·채팅 등 이벤트, 타임아웃은 None
canvas.execute("python", "print(input())", stdin="hello\n")
```

`execute()`는 기존 런타임 API를 호출해 결과를 반환합니다. 브라우저의 블루프린트 실행 계획을 자동 실행하거나 결과 객체에 출력을 자동 작성하지는 않습니다. 결과 객체를 갱신하려면 기존 `runtimeResults` 스키마로 `patch()`하세요. JSON 외 파일 응답은 `client.request("GET", "/api/...", raw=True).content`로 받을 수 있고 multipart 업로드는 `request(..., files=...)`로 기존 API를 호출할 수 있습니다. 봇의 HTTP 허용 경로는 캔버스 조회·접속·실행 등으로 제한되므로 추가 관리 API가 필요한 작업은 실제 사용자 계정을 사용하세요. 그 밖의 브로커 기능은 `send_event()`로 기존 이벤트를 전달할 수 있습니다. 응답이 없는 커서 등 일방향 이벤트는 `publish()`를 사용합니다.

## 설정과 권한

```python
canvas.settings()
canvas.invite("참여자", 1)
canvas.remove_participant("참여자", 1)
canvas.update_settings("description", value="Python에서 수정")
canvas.update_settings("group_permissions", value={
    "default": {"read":True,"write":False,"execute":False},
    "admin-group": {"read":True,"write":True,"execute":True},
})
```

설정 변경은 기존 `expected_revision` 충돌 검사를 따르며 관리자 권한이 필요합니다. SDK 0.1.3부터 설정 변경은 재접속 없이 반영하고 `canvas.capabilities`도 실시간 갱신합니다. 접근 권한이 회수되면 객체를 지우고 연결을 종료하며 자동 재접속하지 않습니다. 현재 권한으로 거부된 작업은 `AgoraError.code`로 식별할 수 있습니다.

## 동기화와 실패 처리

객체 변경은 서버의 저장 커밋 또는 저장 영수증을 확인한 스냅샷을 받은 뒤 반환합니다. 한 번에 한 변경만 전송하고, 재연결 시 **같은 client_id·request_id·request_count·내용**으로 재시도합니다. 단순 ACK만으로 성공 처리하지 않습니다. revision 누락은 스냅샷으로 복구합니다. 최대 512개 작업/8MiB 한도를 넘는 요청은 나눠 보내세요. 로컬 복사본은 낙관적으로 변경하지 않습니다.

`OutcomeUnknown`은 실패 확정이 아닙니다. 동일 작업을 새 요청으로 보내지 말고 `wait_for_idle()` 또는 `refresh()`로 저장 여부를 확인하세요. 인증·권한이 회수되면 재접속을 중단합니다. 이벤트 큐는 최근 256개로 제한되며 전체 객체 데이터는 `.items`에 있으므로 이벤트 소비가 느리면 `refresh()`로 상태를 확인하세요. 동기 메서드는 여러 스레드에서 호출할 수 있지만 이벤트 소비자 하나를 권장합니다.

```bash
python -m unittest discover -s tests
```


새 객체의 `permission`을 생략하면 현재 사용자의 모든 소속 그룹(`member_groups`)이 기본 적용됩니다. 읽기 전용 그룹도 공유 대상에 포함되지만 편집 권한은 별도로 검사합니다. 공통 그룹이 없는 일반 사용자는 객체를 조회하거나 실시간 변경을 받을 수 없습니다. 캔버스 관리자는 전체 객체에 접근할 수 있습니다.


그룹은 관리자·기본 그룹을 포함해 최대 64개입니다. `canvas.capabilities["group_bits"]`는 그룹별 고정 비트 번호(0~63), `canvas.capabilities["group_masks"]`는 member/read/write/execute 마스크의 16자리 16진 문자열입니다. `int(mask, 16)`으로 정밀도 손실 없이 정수로 바꿀 수 있습니다. 그룹 권한은 읽기=1, 쓰기=2, 실행=4의 조합(0~7)으로 저장하며 기존 불리언 설정 요청도 지원합니다.
