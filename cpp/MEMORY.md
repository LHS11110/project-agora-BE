# 범용 메모리 사용

`CanvasServiceMemory → MemoryClass → LruMemory / RedisMemory`

`MemoryClass`는 RedisJSON 문서와 JSON 경로를 다루는 범용 메모리입니다. JSON 경로 조회는 RedisJSON과 같은 배열 응답, 전체 문서 조회는 문서 문자열을 반환하며 실패·누락은 `std::nullopt`로 반환합니다. 서비스별 스키마는 알지 않습니다.

```cpp
MemoryClass memory(host, port, user, password);
memory.write("preferences:7", {{"theme", "dark"}});
auto document = memory.read("preferences:7");
// 내구성이 필요한 경계에서 확인합니다.
if (!memory.flush("preferences:7")) { /* 저장 실패 처리 */ }
```

반복 조회의 두 번째 접근부터 크기·용량 조건을 만족한 문서를 LRU로 승격합니다. 이후 읽기와 수정은 메모리에서 처리하며 퇴거 또는 flush 시 Redis에 반영합니다. 실패한 dirty 값은 버리지 않습니다. 캐시 비활성화·용량 초과는 Redis 경로로 처리합니다. `Options.region_depth`는 캐시할 JSON 하위 트리의 깊이, `remote_fields`는 서비스가 선언한 원격 필드입니다.

기본 State는 인스턴스마다 독립입니다. 동일 백엔드를 사용하는 연결들만 명시적으로 State를 공유해야 합니다. 상태의 마지막 소유자를 파기하기 전에 dirty 데이터를 flush해야 하며 소멸자는 네트워크 저장을 수행하지 않습니다. 여러 서버 프로세스 사이의 캐시 무효화는 제공하지 않습니다. 캔버스는 기존 단일 담당 서버 할당을 사용합니다.

`CanvasServiceMemory`는 키·문서 복원·비밀번호 마이그레이션·채팅 및 설정 Lua를 담당합니다. 원자적 연산은 `MemoryClass::executeAtomic`을 통해 선행 변경을 저장하고 해당 경로를 무효화합니다. `LruMemory`와 `RedisMemory`는 저장소 역할만 담당합니다.
