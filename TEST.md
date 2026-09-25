# 테스트 안내

이 문서는 현재 저장소에 포함된 Spring Boot 및 C++ 테스트가 실제로 실행되는 방법과 검증 범위를 설명합니다. 두 서버 테스트는 외부 시스템을 모두 띄우는 종단 간 테스트가 아니라, H2·모의 객체·로컬 가짜 서버를 사용해 각 서버의 로직을 검증합니다.

## 실행 방법

### Spring Boot

저장소 루트에서 다음을 실행합니다.

```bash
cd spring
./gradlew test
```

Windows에서는 `./gradlew` 대신 `gradlew.bat test`를 실행합니다. Gradle 테스트 리포트는 `spring/build/reports/tests/test/index.html`에 생성됩니다.

자동 테스트는 `spring/src/test/resources/application.properties`를 사용합니다. SQL Server 대신 MSSQL 호환 모드의 H2 메모리 DB를 사용하고, Spring 테스트 컨텍스트 수명에 맞춰 스키마를 생성·삭제합니다. Redis와 Elasticsearch는 기본적으로 외부 인스턴스에 연결하지 않습니다.

### C++

저장소 루트에서 CMake로 테스트 포함 빌드 후 CTest를 실행합니다.

```bash
cmake -S cpp -B cpp/build -DBUILD_TESTING=ON
cmake --build cpp/build -j2
ctest --test-dir cpp/build --output-on-failure
```

`ctest`는 단위 테스트와 로컬 Redis Sentinel 장애 전환 테스트를 실행합니다. 두 번째 테스트는 임의의 loopback 포트를 열어 가짜 Redis/Sentinel 서버를 띄우므로, 실행 환경에서 로컬 TCP 소켓 사용이 허용되어야 합니다. CTest의 통합 테스트 제한 시간은 20초입니다.

## Spring Boot 테스트 범위

| 테스트 영역 | 주요 검증 내용 |
| --- | --- |
| 애플리케이션 컨텍스트 | Spring 애플리케이션 컨텍스트가 H2 테스트 설정에서 시작되는지 확인합니다. |
| 인증·보안 | 회원가입과 로그인 성공·실패, JWT 생성·검증·만료, 공개/보호 API의 인증 응답을 확인합니다. |
| 캔버스 API·서비스 | 캔버스 생성·조회·삭제, 참여자 변경, 비밀번호 확인, 활성 캔버스의 접근·수정 제한 및 서버 할당 흐름을 검증합니다. 외부 의존성은 모의 객체로 대체합니다. |
| 캔버스 비밀번호 | BCrypt 저장 형식, 레거시 비밀번호 정규화, C++ 서버와 공유하는 PBKDF2 형식의 검증을 확인합니다. |
| 로드 밸런서 | 서버/Redis 후보가 없을 때의 오류와 단일 후보 선택, P2C 방식의 부하가 낮은 후보 선택을 확인합니다. |
| Elasticsearch 서비스 | REST 요청의 색인·삭제·조회, 인덱스 부재, 통신 실패 및 `fail-on-error` 동작을 모의 응답으로 검증합니다. |
| Redis Sentinel 장애 전환 | 실제 `CanvasRedisDocumentReader`가 인증을 요구하는 로컬 가짜 Sentinel과 Redis 노드 A/B에 RESP로 접속합니다. Sentinel 별도 계정 인증, A 중단 후 읽기 실패 및 장애 로그 상태를 확인하고, Sentinel이 B를 새 primary로 알리면 문서를 다시 읽고 복구·primary 변경 로그 호출을 확인합니다. 로그 서비스는 모의 객체입니다. |
| SQL Server AG 모니터 | 모의 `DataSource`가 먼저 연결 실패를 반환하고 이후 SQL Server 메타데이터 및 `SELECT @@SERVERNAME` 결과를 반환하도록 구성합니다. 장애·복구와 primary 이름 변경에 따른 로그 서비스 호출을 검증합니다. |
| 선택적 Elasticsearch 실연동 | `CanvasElasticsearchIntegrationTest`는 `ES_USER_PASSWORD`가 설정되고 `localhost:9200`의 클러스터가 응답할 때만 실행됩니다. `ES_USER_NAME`은 선택 항목이며 기본값은 `agora_user`입니다. `canvas` 인덱스에 고정 ID `9876`, 이름 `Agora-Integration-Test-Canvas` 문서를 저장·조회·수정·삭제하고 정리합니다. 조건이 충족되지 않으면 테스트가 실패하는 대신 건너뜁니다. |

관련 코드는 `spring/src/test/java` 아래에 있습니다. 특히 장애 전환 테스트는 각각 `CanvasRedisDocumentReaderFailoverTest`와 `HaFailoverMonitorTest`입니다.

## C++ 테스트 범위

| 실행 대상 | 실제 검증 내용 |
| --- | --- |
| `agora_cpp_unit_tests` | 캔버스 비밀번호 해시 형식 판별 및 레거시 값 정규화, 한 사용자의 여러 WebSocket 연결 수와 마지막 연결 해제 시 활성 상태 변경, 영속화 큐의 순서·barrier·unload 후 enqueue 거부, SQL 명령에서 악성 입력 문자열이 쿼리 본문이 아닌 바인딩 값으로 유지되는지 확인합니다. |
| `agora_cpp_redis_failover_integration` | 실제 `RedisClient`를 인증이 필요한 가짜 Sentinel과 가짜 Redis primary A/B에 연결합니다. Sentinel 인증과 Redis 인증·ROLE 확인·PING을 검증하고, A가 read-only replica로 바뀌면 기존 연결의 실패를 확인합니다. 이후 Sentinel이 B를 primary로 안내하면 클라이언트가 새 노드를 찾고 연결을 복구해 PING에 성공하는지 확인합니다. |

C++ 통합 테스트는 별도의 Redis, Sentinel, SQL Server, Elasticsearch를 실행하지 않습니다. C++ 서버 전체를 구동하는 WebSocket 종단 간 테스트도 아닙니다. `cpp/tests/unit_tests.cpp`와 `cpp/tests/redis_failover_integration.cpp`가 테스트 본체입니다.

## 해석 시 유의할 점

- SQL Server AG 테스트는 연결 실패·복구·primary 변경을 모의 입력으로 검증합니다. 실제 SQL Server AG의 노드 장애나 JDBC/Hikari 연결 복구를 수행하지 않습니다.
- C++ SQL 매개변수 단위 테스트는 문자열 값이 SQL 본문과 분리되는지 확인합니다. 실제 SQL Server에 RPC를 전송하는 통합 테스트는 포함하지 않습니다.
- Redis 장애 전환은 클라이언트의 Sentinel 조회와 재접속 로직을 가짜 로컬 서버로 재현합니다. 운영 Redis Stack HA 클러스터 자체를 검증하지 않습니다.
- 위 장애 감지 테스트는 Elasticsearch에 로그 문서를 실제 저장하는 테스트가 아닙니다. 로그 서비스 메서드 호출을 모의 객체로 확인합니다. Elasticsearch 실연동 테스트는 캔버스 문서 CRUD를 확인합니다.
- Elasticsearch 실연동 테스트를 실행하는 경우 테스트 인덱스의 ID `9876` 문서를 삭제하므로, `localhost:9200`이 테스트용 클러스터인지 확인해야 합니다.
