# Google 로그인

Google Cloud에서 OAuth 클라이언트 유형을 웹 애플리케이션으로 만들고 다음 URI를 정확하게 등록합니다.

- 서비스: https://frelog.p-e.kr
- 승인된 리디렉션 URI: https://frelog.p-e.kr/api/auth/oauth2/callback/google
- 동의 화면의 앱 이름·지원 이메일·개인정보 처리방침을 설정합니다. 테스트 모드라면 로그인할 Google 계정을 테스트 사용자로 추가합니다.

BE `.env`에 `GOOGLE_CLIENT_ID`, `GOOGLE_CLIENT_SECRET`, `GOOGLE_REDIRECT_URI`를 지정하고 `GOOGLE_OAUTH_ENABLED=true`로 설정합니다. Secret은 FE, Git, 공유 로그에 넣지 않습니다. 기본 상태는 비활성이고, 비활성 상태에서는 로그인 화면의 Google 버튼을 표시하지 않습니다. 설정 후 Spring 컨테이너를 재생성해야 합니다.

## 흐름

1. FE는 `GET /api/auth/providers`로 활성 제공자를 확인합니다.
2. Google 버튼은 `/api/auth/oauth2/authorize/google`로 이동합니다.
3. Spring Security OIDC가 authorization code, state, nonce, PKCE, 서명·issuer·audience 검증을 처리합니다. callback 주소는 설정된 HTTPS 주소를 사용하며 Host 헤더로 만들지 않습니다.
4. 확인된 Google `sub`를 `oauth_provider=google`과 함께 식별 키로 사용합니다. 최초 로그인 때만 일반 사용자와 세션을 생성합니다. 비밀번호는 저장하지 않습니다. 확인되지 않은 이메일과 정지·탈퇴 계정은 거절합니다.
5. 이메일이 기존 계정과 충돌하면 자동 병합하지 않고 기존 로그인 사용을 안내합니다. 기존 계정 연결 기능은 별도 구현 대상입니다. Google 이메일이 바뀌어도 기존 `sub`로 동일 계정을 찾습니다.
6. 로그인 성공 후 60초짜리 HttpOnly·Secure·SameSite=Lax 세션으로 `/login?oauth=success`에 돌아옵니다. FE는 `GET /api/auth/oauth2/session`을 한 번 호출해 기존 FreLog JWT를 받습니다. 세션은 즉시 폐기하고 응답은 no-store로 처리합니다. JWT·Google token·Client Secret을 리디렉션 URL에 넣지 않습니다.
7. 이후 API·캔버스 통신은 기존 JWT 인증을 사용합니다. 일반 API는 OAuth 세션만으로 인증되지 않습니다. Google API용 refresh token을 요청·영구 저장하지 않습니다.

DB `mssql/migrations/2026-10-31-oauth-identity.sql`은 제공자와 subject의 중복 연결을 차단하는 filtered unique index를 추가합니다. 기존 계정은 변경하거나 자동 연결하지 않습니다. DB 초기화 SQL에도 동일 인덱스를 반영했습니다.

Spring은 내부 서비스 네트워크와 별도의 `agora-oauth-egress` NAT 네트워크에 연결됩니다. Google token/JWKS/userinfo 서버에 HTTPS로 접근하기 위한 구성이며 호스트 포트를 공개하지 않습니다. DB와 C++은 이 네트워크에 연결하지 않습니다.

Google Cloud 자격 증명이 없는 환경에서는 서버 단위·통합 테스트와 FE의 mocked-provider 브라우저 테스트를 실행할 수 있습니다. 실제 Google 동의 화면부터 성공 콜백까지의 검증에는 유효한 자격 증명과 사용자의 Google 로그인이 필요합니다.

참고: [Google OpenID Connect](https://developers.google.com/identity/openid-connect/openid-connect), [Spring Security OAuth2 Login](https://docs.spring.io/spring-security/reference/servlet/oauth2/login/advanced.html).

운영 검증: 2026-10-31 Google 로그인 활성화, 실제 Google 인증 화면 이동과 리디렉션 URI 일치, PKCE/state/nonce, 세션 쿠키 속성을 확인했습니다. Spring에서 Google token/JWKS 서버로의 HTTPS 연결도 확인했습니다. Google 계정 선택·동의 후 최종 성공 로그인은 사용자가 직접 확인해야 합니다.

리디렉션 포트 수정: 외부 호스트 Nginx는 실제 HTTPS 포트와 호스트를 전달하며, Docker 게이트웨이는 신뢰한 외부 프록시의 포트만 유지합니다. OAuth 성공·실패·계정 충돌 후 복귀 주소는 검증된 Google callback URI의 origin으로 고정하여 내부 포트나 요청 헤더가 브라우저 복귀 주소를 바꾸지 못하게 합니다.
