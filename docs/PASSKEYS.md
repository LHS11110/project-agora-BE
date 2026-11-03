# 패스키 인증

프로필 → **패스키**에서 기기 이름을 입력하고 **패스키 추가**를 누릅니다. 지문·얼굴·화면 잠금 PIN 또는 호환 보안 키의 사용자 확인을 완료하면 등록됩니다. 최신 HTTPS 브라우저가 필요합니다. 계정마다 최대 10개를 등록하고 각각 삭제할 수 있습니다. Google 로그인으로 생성한 계정도 등록할 수 있습니다.

로그인 화면의 **패스키로 로그인**은 이메일과 비밀번호 없이 등록된 패스키로 사용자를 식별합니다. 서버가 패스키 서명과 기기의 사용자 확인을 검증하면 MFA 설정 여부와 관계없이 추가 인증앱 코드 없이 JWT를 받습니다. 기존 비밀번호·Google 로그인과 MFA 동작은 유지됩니다. 패스키만으로 새 계정을 생성하지는 않습니다.

## 서버 검증과 저장

- Yubico `webauthn-server-core:2.10.1`이 WebAuthn challenge, RP ID 해시, 정확한 origin, 사용자 존재/확인, 서명과 서명 카운터를 검증합니다. 사용자 확인(`userVerification`)과 검색 가능한 자격 증명(`residentKey`) 등록을 필수로 요청합니다.
- RP ID `frelog.p-e.kr`, origin `https://frelog.p-e.kr`을 고정합니다. 요청 Host나 전달된 헤더로 신뢰할 origin을 만들지 않습니다. 포트·서브도메인 완화도 사용하지 않습니다.
- 개인 키와 지문·얼굴 정보는 기기/패스키 제공자가 관리하며 서버로 전송하지 않습니다. SQL에는 credential ID, COSE 공개 키, 무작위 사용자 handle, 카운터, 이름과 사용 시각을 저장합니다. API 목록에는 공개 키와 credential ID도 노출하지 않습니다.
- 256비트 무작위 요청 토큰의 SHA-256 해시, 서버가 만든 원본 옵션, 사용자/용도/IP, 5분 만료와 사용 여부를 SQL에 저장합니다. HttpOnly·Secure·SameSite=Strict 쿠키로 브라우저도 연결합니다. 검증에 사용한 요청은 실패해도 재사용하지 못합니다.
- 서명 검증 시 자격 증명 행과 요청 행을 잠그며, 최신 카운터를 갱신합니다. 정지·탈퇴 계정은 로그인할 수 없습니다. 재가입한 계정은 다른 사용자 ID/handle을 사용하므로 이전 계정의 패스키나 권한을 승계하지 않습니다.
- 등록·삭제는 로그인 후 10분 이내 JWT가 필요합니다. MFA 사용자는 인증앱 또는 검증된 패스키로 로그인한 토큰이어야 합니다. 모든 변경 API에서 정확한 Origin도 확인합니다. 요청 생성은 IP당 분당 20회로 제한하고, 만료된 요청은 정리합니다.

## 배포

1. DB 관리자 권한으로 `mssql/migrations/2026-11-01-passkeys.sql`을 적용합니다(`TABLE_USERS=users`). 신규 설치용 `init-mssql.sql`에도 포함됩니다.
2. BE `.env`의 선택 설정 `PASSKEY_RP_ID`, `PASSKEY_ORIGIN`을 Compose가 Spring에 전달합니다. 기본값은 현재 운영 도메인입니다. 외부 인증업체의 Client Secret은 필요하지 않습니다.
3. Spring과 프런트엔드를 빌드·배포합니다. 도메인이 바뀌면 RP ID에 묶인 기존 패스키를 새 도메인에서 그대로 사용할 수 없으므로 기존 로그인 방식으로 로그인한 후 다시 등록합니다.

## API

- 공개: `GET /api/auth/passkeys/status`, `POST /login/options`, `POST /login/verify`
- 로그인 필요: `GET /api/auth/passkeys`, `POST /registration/options`, `POST /registration/verify`, `DELETE /{id}` (이하 경로는 `/api/auth/passkeys` 기준)
- 옵션 응답: `{challengeToken, options: {publicKey: ...}}`
- 검증 요청: `{challengeToken, credential: WebAuthn JSON, name?: string}`
- 로그인 검증 응답은 기존 `LoginResponse` 형식의 JWT 응답와 같습니다. 모든 응답은 `Cache-Control: no-store`입니다.

## 검증

기본 Spring 테스트와 함께 실제 Chromium 가상 CTAP2 인증기를 사용합니다. 서버의 서명 검증과 브라우저 UI 테스트를 구분하며, 실제 사용자 계정을 변경하지 않습니다.

```sh
AGORA_PASSKEY_BROWSER=1 AGORA_E2E_PLAYWRIGHT_MODULE=/path/to/playwright-core/index.mjs \
  JAVA_HOME=/path/to/jdk26 ./spring/gradlew -p spring test
```

FE의 `tests/integration/passkeys.mjs`는 실제 브라우저 WebAuthn API와 가상 인증기를 사용하고, API만 모킹하여 등록·목록·삭제·로그인 복귀 경로·MFA 활성 계정의 패스키 직접 로그인을 확인합니다. 구형 브라우저용 base64url 변환 경로도 지원합니다.

라이브러리: https://developers.yubico.com/java-webauthn-server/
