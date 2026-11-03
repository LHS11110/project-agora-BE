# 회원가입 이메일 소유 확인

이메일 가입 화면에서 메일로 받은 6자리 코드를 확인한 뒤 비밀번호·닉네임과 함께 가입합니다. **인증 전에는 사용자·세션·접근 토큰을 만들지 않습니다.** 형식 검사나 MX 조회만으로 소유를 확인했다고 처리하지 않습니다.

## 발송 설정

BE `.env`의 다음 값을 실제 SMTP 제공자 설정으로 입력합니다. 비밀번호는 Git/FE/공유 로그에 넣지 않습니다.

```dotenv
MAIL_HOST=smtp.example.com
MAIL_PORT=587
MAIL_USERNAME=SMTP에서 발급받은 사용자명
MAIL_PASSWORD=SMTP용 비밀번호
MAIL_FROM=noreply@example.com
MAIL_SECURITY=starttls
```

465 포트의 암시적 TLS는 `MAIL_SECURITY=ssl`을 사용합니다. 평문 SMTP·인증 없는 접속·서버 신원 검증 무시는 제공하지 않습니다. From 주소는 제공자가 발신을 허용한 주소여야 합니다. Google OAuth의 Client Secret은 SMTP 비밀번호가 아닙니다. SPF/DKIM·도메인 발신 정책은 사용하는 메일 제공자에서 구성합니다.

설정 후 런타임 볼륨 override를 보존해 Spring을 재생성합니다(서버의 현재 운영 구성 기준).

```bash
cd /home/steam/Github/project-agora-BE
docker compose --env-file .env -f docker-compose.backend.yml -f docker-compose.runtime.yml up -d --no-deps --force-recreate --wait spring
```

`EMAIL_VERIFICATION_SECRET`은 서버에서 생성한 Base64 32바이트 비밀키입니다. 이미 구성했으므로 그대로 보관합니다. 다중 Spring 인스턴스에서는 같은 키를 사용해야 합니다. 키를 바꾸면 이전 인증 코드 검증이 실패하므로 새 메일을 요청해야 합니다. 이메일 코드 자체를 저장하거나 로그에 출력하지 않습니다.

SMTP 정보가 없거나 발신 주소가 유효하지 않으면 `/api/auth/email/status`는 `available=false`를 반환합니다. 이는 구성 여부이며 실제 배달 성공이나 스팸함 도달을 보증하는 상태 확인은 아닙니다. 설정이 없거나 발송이 실패해도 인증을 생략해 가입시키지 않습니다. Google 로그인과 기존 계정 로그인은 유지됩니다. 발신만 사용하며 서버에 메일 수신 포트를 열지 않습니다.

## API와 저장

1. POST `/api/auth/email/request` `{ email }` → 202 `{ challengeId, expiresInSeconds:600, resendAfterSeconds:60 }`.
2. POST `/api/auth/email/verify` `{ email, challengeId, code }` → `{ emailVerificationToken, expiresInSeconds:600 }`.
3. POST `/api/auth/signup` 또는 호환 별칭 `/api/users`에 `{ email, password, nickname, emailVerificationToken }`을 전달합니다. 인증 증명과 새 계정 생성을 한 트랜잭션으로 처리합니다.

코드와 가입 증명 모두 한 번만 사용합니다. 재전송하면 이전 코드와 가입 증명을 무효화합니다. 인증을 끝냈더라도 가입 증명 만료 후에는 다시 인증해야 합니다. 이메일을 바꾸면 이전 증명을 사용할 수 없습니다. 프런트엔드는 이메일 변경 중 도착한 오래된 응답도 버립니다.

수신 주소는 trim/lowercase로 통일합니다. DB는 `auth_email_verification`과 `auth_email_ip_quota`를 사용합니다. 코드는 비밀키 HMAC-SHA256으로, 가입용 무작위 256비트 증명은 SHA-256으로 저장합니다. 코드 5회 오류 이후 해당 challenge를 사용할 수 없습니다. 재발송 최소 60초, 주소별 5회/시간, 요청 IP별 20회/시간으로 발송을 제한하며 카운터는 실패해도 유지합니다. 원본 IP는 신뢰된 프록시 경로로 전달된 서버 요청 주소를 사용합니다.

기존 계정의 가입 메일 요청에는 `409 USER_001` 중복 계정 안내를 반환하며 코드를 보내거나 기존 계정을 수정하지 않습니다. 만료된 증명과 집계는 정기 정리합니다. Google 가입은 OIDC의 `email_verified=true`를 확인한 뒤 처리합니다. 새로 확인된 이메일은 `users.email_verified_at`에 기록합니다. 기존 계정 전체를 소급 인증 완료로 표시하거나 강제로 막지는 않습니다.

DB 관리자는 `mssql/migrations/2026-10-31-email-verification.sql`을 적용합니다. 초기화 SQL에도 반영했습니다. 운영 Hibernate validate를 유지하며 자동 DDL이나 인증 우회 스위치를 추가하지 않았습니다. 기존 부하 스크립트에서 가입 API를 직접 호출하던 부분은 이제 사전 이메일 인증 증명을 준비해야 합니다. 운영 환경에 테스트용 인증 우회는 제공하지 않습니다.

## 검증

서버 통합 테스트는 가입 전 계정 미생성, 두 가입 경로의 증명 필수 여부, 이메일 바인딩·정규화·일회성, 만료·재발송·시도 제한, 발송 실패, 가입 실패 시 증명 소비 롤백, 기존 계정 미변경을 확인합니다. 실제 로컬 TLS SMTP fixture에서 STARTTLS와 암시적 TLS의 인증·UTF-8 코드 전송을 검증합니다. 브라우저 테스트는 코드 입력·재전송 대기·잘못된 코드·만료 증명 복구·이메일 변경 중 오래된 응답 폐기와 설정 없는 상태를 확인합니다. 외부 SMTP/실제 수신함 검증은 사용자가 발송 정보를 입력한 뒤 진행해야 합니다.

참고: [OWASP 이메일 검증](https://cheatsheetseries.owasp.org/cheatsheets/Email_Validation_and_Verification_Cheat_Sheet.html), [Spring Mail](https://docs.spring.io/spring-boot/reference/io/email.html).

운영 검증(2026-11-01): DB 마이그레이션과 FE/Spring 배포를 완료했습니다. 설정된 SMTP에 TLS 연결·인증 후 NOOP만 보내는 점검을 통과했습니다. 실제 메일은 임의로 발송하지 않았습니다. 운영 API에서 인증 증명 없는 두 가입 경로와 가짜 증명을 거절하는 것을 확인했습니다. 브라우저의 전체 가입/코드 흐름은 메일 API를 대체한 테스트로 검증했으며 실제 수신함 배달과 코드는 사용자가 가입 화면에서 확인할 수 있습니다.

이미 등록된 이메일의 인증 요청은 `409 USER_001` 중복 계정 안내를 반환하고 메일이나 가짜 challenge를 발급하지 않습니다. OAuth 가입도 같은 정책이며 계정 자동 연결은 하지 않습니다.
