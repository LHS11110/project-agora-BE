# TLS 초기 설정

기본 Docker 구성은 HTTPS·WSS와 인증서 검증을 사용하는 DB TLS 연결입니다. 인증서는 직접 준비하며 설정 도구는 인증서를 발급하지 않습니다.

네 프로젝트를 처음 실행할 때는 [전체 clone·인증서 배치·실행 안내](../project-agora-FE/docs/TLS_SETUP.md)를 따르세요. 기본 인증서 디렉터리 구조와 SAN 목록도 해당 문서에 있습니다.

BE 저장소에서 DB와 BE 설정만 준비할 수도 있습니다.

```bash
python3 scripts/setup-docker.py \
  --db-dir ../project-agora-DB \
  --tls-dir /path/to/certificates
git submodule update --init --recursive
```

설정 명령은 DB `.env` 준비, 인증서 체인·키·SAN 검증, BE `.env` 생성과 비밀번호·CA 동기화를 수행합니다. 기존 비밀번호를 보존하고 비밀값은 출력하지 않습니다. `--public-origin https://app.example.com`으로 게이트웨이 주소를 바꿀 수 있으며 해당 도메인을 인증서 SAN에 포함해야 합니다.

DB를 먼저 실행·초기화한 뒤 Wall의 `scripts/setup.py`로 `.env`를 준비하고 `scripts/storage-broker-docker.sh up`을 실행합니다. 이어서 BE의 `scripts/backend-docker.sh up`, Wall의 `scripts/broker-docker.sh up` 및 `scripts/nginx-docker.sh development` 순서로 시작합니다. FE에는 별도로 `.env.example` 복사 또는 `setup-projects.py` 실행이 필요합니다. 인증서가 없으면 시작에 실패하며 평문으로 전환하지 않습니다. [저장소 브로커의 네트워크·라우팅·기존 환경 전환](../project-agora-Wall/STORAGE_BROKER.md)을 참고하세요.

## TLS 프로토콜 정책

웹 HTTPS/WSS, Wall nginx, Phoenix 브로커, Spring, C++의 HTTPS/WSS·Redis·Elasticsearch 연결과 Redis/Sentinel·Elasticsearch 서버는 TLS 1.3을 사용합니다. TLS 1.2로의 하향 연결은 허용하지 않습니다. 기존 CA 및 인증서를 그대로 사용할 수 있으며, 설정 반영에는 해당 서비스 재빌드·재시작이 필요합니다.

예외는 현재 SQL Server 2022 Linux 컨테이너입니다. [Microsoft의 지원 문서](https://learn.microsoft.com/en-us/sql/linux/sql-server-linux-known-issues?view=sql-server-ver17#tls-13-not-supported-on-sql-server-2022)에 따라 이 의존성은 TLS 1.3을 지원하지 않으므로 SQL 연결은 인증서 검증을 수행하는 TLS 1.2 암호화를 유지합니다. SQL까지 TLS 1.3을 요구하려면 지원되는 SQL 서버 환경 및 TDS 드라이버로 별도 전환해야 합니다. SQL 호환성을 위해 JVM 전체에 TLS 1.3 전용 설정을 적용하지 않고 연결별로 제한합니다.
