# C++ API 추가 방식

HTTP 서버는 공통 전송 설정만 관리하고, 기능별 경로는 `HttpApiModule`로 분리합니다. 새 API는 기존 경로를 수정하지 않고 별도 모듈로 추가할 수 있습니다.

## 새 기능 추가

1. `include/<Feature>Api.hpp`와 `src/api/<Feature>Api.cpp`에 `HttpApiModule` 구현을 추가합니다.
2. 모듈은 요청 검증과 응답 변환을 맡고, 업무 규칙은 별도 Service에 둡니다.
3. Service는 기능별 Repository 인터페이스에 의존하게 하고, MSSQL·Redis·Elasticsearch 접근 구현은 Repository/adapter에 둡니다. 공용 저장소 포트로 `SqlExecutor`, `KeyValueStore`, `JsonDocumentStore`를 사용할 수 있습니다.
4. `main.cpp`의 조립부에서 Service와 Repository를 만들고 API 모듈에 주입한 다음 등록합니다.

이 경계를 유지하면 HTTP 계약은 저장소 명세에 덜 영향을 받고, 저장소 스키마가 바뀔 때 주로 해당 Repository/adapter를 변경하면 됩니다.

## 제공되는 저장소 접근

- `MssqlClient`는 `SqlExecutor`를 구현하며 `SqlCommand`를 공용 연결 풀로 실행합니다. SQL 값은 바인딩 파라미터로 전달하고, 조회 결과는 컬럼명과 nullable 문자열 값으로 반환합니다. `addText`는 일반 길이 문자열, `addMaxText`는 최대 1 MiB 텍스트를 지원합니다. 숫자·날짜 등 컬럼 타입 변환은 Repository에서 수행합니다.
- `CanvasMemory`는 캔버스·아이템 조회/저장, LRU 승격·변경분 병합·flush·무효화를 담당하며 `KeyValueStore`도 구현합니다. API는 이 클래스의 캔버스/아이템 연산을 호출하고 Redis 키·JSON 경로·명령을 만들지 않습니다.
- `RedisClient`는 `RedisCommandExecutor`를 구현하며 Sentinel 탐색, 검증된 TLS, RESP 명령/응답만 담당합니다. LRU와 캔버스 문서 규칙은 포함하지 않습니다. `CanvasMemory`마다 독립적인 연결을 소유하고 프로세스의 제한된 LRU 및 문서 잠금만 공유합니다. 전송 객체를 주입하여 외부 Redis 없이 메모리 정책을 검사할 수 있습니다.
- `EsClient`는 `JsonDocumentStore`를 구현해 인덱스 이름과 문서 ID를 받는 일반 문서 조회·저장·부분 수정·삭제·검색 연산을 제공합니다. 캔버스 전용 인코딩과 비밀번호 규칙은 기존 Canvas 메서드에 남겨 둡니다.

SQL은 고정된 Repository 코드에 작성하고 사용자 입력은 `SqlCommand` 파라미터로 전달합니다. 테이블명이나 컬럼명처럼 파라미터 바인딩할 수 없는 식별자는 요청값으로 만들지 않습니다.
