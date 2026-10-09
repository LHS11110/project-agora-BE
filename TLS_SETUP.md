# TLS 초기 설정

기본 Docker 구성은 HTTPS·WSS와 인증서 검증을 사용하는 DB TLS 연결입니다. 인증서는 직접 준비하며 설정 도구는 인증서를 발급하지 않습니다.

세 프로젝트를 처음 실행할 때는 [전체 clone·인증서 배치·실행 안내](../project-agora-FE/docs/TLS_SETUP.md)를 따르세요. 기본 인증서 디렉터리 구조와 SAN 목록도 해당 문서에 있습니다.

BE 저장소에서 DB와 BE 설정만 준비할 수도 있습니다.

```bash
python3 scripts/setup-docker.py \
  --db-dir ../project-agora-DB \
  --tls-dir /path/to/certificates
git submodule update --init --recursive
```

설정 명령은 DB `.env` 준비, 인증서 체인·키·SAN 검증, BE `.env` 생성과 비밀번호·CA 동기화를 수행합니다. 기존 비밀번호를 보존하고 비밀값은 출력하지 않습니다. `--public-origin https://app.example.com`으로 게이트웨이 주소를 바꿀 수 있으며 해당 도메인을 인증서 SAN에 포함해야 합니다.

DB를 먼저 시작한 뒤 `./scripts/backend-docker.sh up`, `./scripts/nginx-docker.sh development`로 실행합니다. FE에는 별도로 `.env.example` 복사 또는 `setup-projects.py` 실행이 필요합니다. 인증서가 없으면 시작에 실패하며 평문으로 전환하지 않습니다.
