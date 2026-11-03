# 선택형 인증앱 MFA

프로필의 **2단계 인증 → 인증앱 연결하기**에서 Google Authenticator 등 호환 앱에 QR 코드를 등록합니다. 설정 키 직접 입력도 지원합니다. QR은 브라우저에서 생성하며 외부 QR 서비스로 키를 보내지 않습니다. 6자리 코드 확인 후 활성화하고, 그때 표시되는 일회용 복구 코드 10개를 안전하게 저장합니다. 화면을 닫으면 기존 복구 코드를 다시 볼 수 없습니다.

이메일/비밀번호와 Google·네이버·카카오 OAuth 로그인 모두 MFA가 활성화된 계정에는 추가 확인을 요구합니다. Google 계정 자체의 2단계 인증과는 별개입니다. 패스키는 서버가 서명과 기기의 사용자 확인(UV)을 필수로 검증하면 추가 인증앱 코드 없이 로그인합니다. 이때도 JWT를 현재 MFA 설정 버전에 연결하여 설정 변경 시 기존 토큰 검증을 유지합니다. 기존 계정에 자동으로 활성화하지 않습니다. 등록·등록 확인에는 10분 내에 발급된 로그인 토큰이 필요하며 오래됐으면 다시 로그인 버튼을 제공합니다.

## API

모든 MFA 응답은 no-store입니다. `/verify`를 제외한 경로는 JWT 인증을 요구합니다.

| 경로 | 용도 |
| --- | --- |
| GET `/api/auth/mfa` | 활성 상태·설정 가능 여부·남은 복구 코드 수 |
| POST `/api/auth/mfa/enrollment` | 10분짜리 등록 키/otpauth URI 발급 |
| POST `/api/auth/mfa/enrollment/confirm` `{code}` | 활성화, 새 로그인 응답과 복구 코드 10개 반환 |
| DELETE `/api/auth/mfa/enrollment` | 진행 중인 등록 취소 |
| POST `/api/auth/mfa/verify` `{challengeToken,code}` | 로그인 추가 인증 완료 |
| POST `/api/auth/mfa/recovery-codes` `{code}` | 코드 확인 후 복구 코드 전체 교체 |
| POST `/api/auth/mfa/disable` `{code}` | 코드 확인 후 비활성화 및 키/복구 코드 제거 |

MFA가 필요하면 기존 로그인 응답은 `mfaRequired=true`, `challengeToken`을 반환하고 `accessToken`과 `user`는 null입니다. 이 challenge는 JWT가 아니며 서비스 접근에 사용할 수 없습니다. 클라이언트는 메모리에만 보관하고, 코드 검증을 완료한 뒤에만 기존 토큰 저장/탐색 흐름으로 넘어갑니다. Google OAuth의 일회용 세션 handoff도 동일한 응답을 전달합니다.

## 저장·검증

- RFC 6238 HMAC-SHA1, 6자리, 30초 주기, 허용 시각 오차 ±1 step. 이미 성공한 step은 다시 인정하지 않습니다.
- TOTP seed: 160비트 무작위 키, AES-256-GCM 암호화, 사용자 ID에 바인딩된 AAD. 복구 코드: 각 80비트 무작위, SHA-256 해시만 저장하며 사용하면 원자적으로 소비합니다.
- 등록·MFA 로그인·해제·복구 코드 재발급의 잘못된 코드 시도를 계정별로 집계합니다. 5회 실패하면 5분 동안 차단하며, 새 로그인 challenge나 등록 요청으로 초기화할 수 없습니다. 실패 카운터는 검증 실패 시에도 DB에 commit됩니다.
- 로그인 challenge는 무작위 256비트 값의 해시만 DB에 저장합니다. 유효기간 5분, 요청 IP와 MFA 설정 version에 바인딩하고 성공 시 한 번만 소비합니다. 오래된 challenge는 정기적으로 정리합니다.
- MFA를 켠 동안 기존 일반 JWT는 보호 API에 접근할 수 없습니다. 검증 완료 토큰에는 현재 MFA version을 넣어 확인합니다. 다시 등록하면 이전 MFA 토큰으로 우회할 수 없습니다. 이미 연결된 캔버스 WebSocket 전체를 즉시 강제 종료하는 기능은 아닙니다.
- 복구 코드 재발급·MFA 해제에는 현재 인증앱 코드 또는 미사용 복구 코드가 필요합니다. 해제 후에는 일반 로그인 정책을 적용합니다.

## 운영 구성

DB `mssql/migrations/2026-10-31-totp-mfa.sql`을 적용하고 Spring을 재생성합니다. DB 초기화 SQL에도 반영되어 있습니다. Hibernate는 운영에서 validate를 유지하며 자동 DDL을 켜지 않습니다.

`MFA_ENCRYPTION_KEY`는 무작위 32바이트를 Base64로 표현한 값입니다. 서버의 BE `.env`에 저장하며, 이 배포에서는 `/etc/agora-secrets/mfa-encryption.key`에도 root 전용(0600) 사본을 보관합니다. 값은 Git·FE·로그에 포함하지 않습니다. **DB와 이 키를 함께 안전하게 백업해야 합니다. 사용자가 등록한 후 임의로 교체하면 기존 키를 복호화할 수 없습니다.** 여러 Spring 인스턴스가 있다면 같은 키와 DB를 사용해야 합니다. 회전 시에는 별도 재암호화 마이그레이션이 필요합니다. 키가 없으면 신규 등록을 허용하지 않습니다.

서버 시계 동기화를 유지합니다. 이 배포에서는 NTP 동기화 상태를 확인했습니다. 사용자가 휴대전화와 복구 코드를 모두 잃은 경우의 관리자 복구/본인확인 절차는 별도 운영 정책이며 자동 우회 경로를 만들지 않았습니다.

## 검증

RFC 테스트 벡터, 등록 키 암호화, JWT/challenge 우회 차단, OTP/복구 코드 재사용, 복구 코드 교체, 재등록 후 기존 MFA token 거절, 만료/IP 검증 및 실패 카운터 지속·잠금 테스트를 포함합니다. 브라우저 테스트는 Google/이메일 MFA 분기와 등록·복구·해제 화면을 확인합니다.

참고: [RFC 6238](https://datatracker.ietf.org/doc/html/rfc6238), [OWASP MFA Cheat Sheet](https://cheatsheetseries.owasp.org/cheatsheets/Multifactor_Authentication_Cheat_Sheet.html).

운영 검증(2026-10-31): 별도 임시 계정으로 실제 HTTPS 브라우저의 QR 등록·MFA 전 토큰 미발급·기존 JWT/challenge 접근 차단·OTP 재사용 거절·복구 코드 로그인/재사용 거절/재발급·MFA 해제를 확인했습니다. 기존 실제 사용자 MFA는 변경하지 않았고 임시 계정 및 연관 MFA/session 기록을 제거했습니다. Google OAuth의 MFA 분기는 서버/브라우저 테스트로 확인했으며, 실제 Google 계정의 MFA 활성화는 사용자가 프로필에서 직접 진행합니다.
