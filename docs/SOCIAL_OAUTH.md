# Google·네이버·카카오 간편 로그인

세 서비스 모두 같은 `/api/auth/oauth2/authorize/{provider}` → `/callback/{provider}` → `/login?oauth=success` 흐름을 사용합니다. `provider`는 `google`, `naver`, `kakao`입니다. `/api/auth/providers`가 활성화 상태를 반환하고, FE는 활성화된 버튼만 표시합니다.

## 운영 설정

BE `.env`에 다음을 지정합니다. Secret은 FE나 Git에 넣지 않습니다.

```dotenv
NAVER_OAUTH_ENABLED=true
NAVER_CLIENT_ID=발급받은_Client_ID
NAVER_CLIENT_SECRET=발급받은_Client_Secret
NAVER_REDIRECT_URI=https://frelog.p-e.kr/api/auth/oauth2/callback/naver

KAKAO_OAUTH_ENABLED=true
KAKAO_CLIENT_ID=카카오_REST_API_키
KAKAO_CLIENT_SECRET=발급받은_Client_Secret
KAKAO_REDIRECT_URI=https://frelog.p-e.kr/api/auth/oauth2/callback/kakao
```

- 네이버 개발자센터: 네이버 로그인 사용 API를 선택하고 서비스 URL `https://frelog.p-e.kr`, Callback URL 위 주소를 등록합니다. 이메일·별명 제공 항목을 설정합니다. 개발 상태에서는 앱에 등록한 테스트 사용자로 먼저 검증하며, 일반 사용자 제공에는 네이버의 검수 조건을 확인합니다.
- 카카오디벨로퍼스: 웹 서비스 도메인 `https://frelog.p-e.kr`, 카카오 로그인 활성화, 위 Redirect URI와 Client Secret을 설정합니다. Client ID에는 JavaScript 키·Admin 키 대신 REST API 키를 사용합니다. 프로필 닉네임·이메일 동의항목은 앱에 허용된 범위에서 설정합니다. OpenID Connect 활성화는 필요하지 않습니다(현재 OAuth 2.0 사용자 정보 API 방식).
- 신청한 동의항목을 사용하며, 서버에서 앱에 허용되지 않은 `account_email` 범위를 강제로 추가하지 않습니다. 이메일이 없더라도 자체 이메일 인증을 거쳐 가입할 수 있습니다.
- Secret과 Callback 설정 후 Spring을 재생성해야 합니다. 개발용 별도 도메인은 모든 활성 서비스의 콜백 origin이 같아야 하며 정확한 HTTPS 주소를 사용합니다.
- 기본값은 신규 두 서비스 비활성입니다. Google 설정·콜백은 그대로 유지합니다.

## 계정과 이메일

- 계정은 이메일이 아니라 `(oauth_provider, oauth_id)`로 식별합니다. 네이버 `response.id`, 카카오 `id`, Google OIDC `sub`를 사용합니다. 기존 이메일이 같아도 자동 계정 연결은 하지 않습니다. 다른 가입 방식으로 이미 활성화된 이메일이라면 중복 계정 안내만 표시합니다.
- 카카오는 `is_email_valid`와 `is_email_verified`가 모두 true인 이메일로 최초 가입할 수 있습니다. Google은 기존 OIDC 검증과 `email_verified` 확인을 유지합니다.
- 네이버는 이메일의 검증 여부 플래그를 제공하지 않습니다. 최초 가입에서는 메일로 받은 코드를 확인한 뒤 가입하며, 인증 전 사용자·세션 JWT를 생성하지 않습니다. 카카오가 이메일을 제공하지 않거나 검증되지 않은 경우도 같은 절차입니다.
- 가입 대기 정보는 5분짜리 서버 세션에만 보관합니다. 브라우저는 일회용 handoff로 이메일 안내와 무작위 enrollment token을 받습니다. POST `/api/auth/oauth2/enrollment`는 같은 세션, 정확한 origin, enrollment token, 수신 이메일에 묶인 일회용 이메일 증명을 모두 검증합니다. 실패 시 계정 생성·증명 소비가 원자적으로 롤백됩니다.
- 이미 가입한 서비스 ID는 프로필 이메일이 바뀌거나 미제공이어도 기존 FreLog 계정으로 로그인합니다. 신규 일반 사용자만 생성하며, 정지 계정은 차단합니다. 탈퇴 시 이메일·비밀번호·OAuth 연결과 인증 정보를 삭제하고 삭제된 계정 표시만 남겨 재가입할 수 있으며 이전 데이터·권한을 복원하지 않습니다.
- MFA 활성 계정의 **모든 OAuth 로그인은 추가 인증앱/복구 코드 확인이 필요**합니다. 패스키의 사용자 확인 후 직접 로그인 정책과 구분합니다.

## 프로토콜과 배포

Spring Security의 authorization-code 흐름과 state 검증을 사용합니다. Google은 OIDC 서명·issuer·audience·nonce도 검증합니다. Google·카카오는 PKCE S256을 적용합니다. 네이버는 명세에 PKCE가 없어 적용하지 않고, HTTPS 서버 간 `client_secret_post`와 저장된 state를 토큰 교환에 포함합니다. Secret과 공급자 토큰을 URL에 넣거나 프런트엔드에 전달하지 않습니다.

콜백 뒤 로그인 목적지는 명시적인 HTTPS 콜백 설정에서 계산하며, 전달된 Host·내부 8444 포트로 만들지 않습니다. 성공 시 이전 세션을 폐기하고 새 세션을 발급합니다. 일반 JWT/MFA handoff는 60초·한 번만 소비하며, 모든 인증 응답은 no-store입니다.

DB 관리자 권한으로 `project-agora-DB/mssql/migrations/2026-11-01-oauth-subject-case.sql`을 적용합니다(`TABLE_USERS=users`). 공급자별 opaque ID를 대소문자 구분해 저장하고 비교하며, 기존 OAuth 고유 인덱스와 탈퇴 계정 인증 제약조건은 유지합니다. 신규 설치용 init SQL에도 포함됩니다.

Orchestra의 기존 production 시작 계획으로 Spring과 FE를 배포합니다. 공급자 인증서는 일반 공개 CA 신뢰 저장소로 확인하고 기존 OAuth egress 네트워크를 공유합니다. 새로운 호스트 포트는 필요하지 않습니다.

## 검증

- `SocialIdentityTest`, `SocialLoginTest`: 응답 형식·이메일 플래그·MFA·이메일 충돌·정지 계정·ID 대소문자 검증.
- `OAuthProviderRegistrationTest`: 서비스별 endpoint·POST secret·PKCE·콜백 origin 설정.
- `SocialOAuthIntegrationTest`: 실제 로컬 HTTP 공급자 서버로 코드/토큰/user-info 교환, 최초 이메일 인증, returning MFA, 재가입, state 변조 차단.
- 기존 Google/OIDC, 이메일 인증, MFA, 패스키 회귀 테스트.
- FE `tests/integration/social-oauth.mjs`: 버튼·경로·비활성 서비스·한 번의 handoff·가입 전 이메일 증명·로그인 복귀·MFA UI. 공급자 API는 모킹합니다.

공급자 콘솔의 실제 Client ID/Secret과 사용자 동의 완료는 별도 실서비스 검증이 필요합니다.

공식 명세: [네이버 프로필](https://developers.naver.com/docs/login/profile/profile.md), [네이버 OAuth](https://developers.naver.com/docs/login/api/api.md), [카카오 REST API](https://developers.kakao.com/docs/ko/kakaologin/rest-api).

## 이미 가입한 이메일 처리

다른 OAuth 식별자로 최초 가입을 시도할 때 제공된 이메일이 활성 계정과 겹치면 `/login?oauth=account_exists`로 돌아가 **이미 존재하는 계정입니다. 기존 로그인 방식을 사용해주세요.** 안내만 표시합니다. 네이버의 이메일이 미검증이어도 중복 여부는 먼저 확인하며, 이 이메일로 기존 계정을 인증하거나 연결하지는 않습니다.

카카오가 이메일을 제공하지 않아 별도로 주소를 입력하는 경우, 입력한 주소가 이미 등록되어 있으면 인증 메일 요청에서 `409 USER_001`을 반환합니다. 메일을 보내거나 가짜 인증 challenge를 반환하지 않습니다. 최종 가입 API도 중복을 다시 검사하므로 이전 버전 화면에서 시도하거나 가입 도중 계정이 생긴 경우에도 연결·덮어쓰기가 발생하지 않습니다. 이메일·비밀번호 일반 회원가입도 중복 계정 안내를 반환합니다.

이미 `(provider, subject)`에 연결된 계정은 중복 가입이 아니라 정상 로그인으로 처리하며 기존 MFA 요구를 유지합니다. 이전의 기존 계정 인증 후 연결 기능은 제공하지 않습니다.

현재 탈퇴/재가입 및 JWT 사용자 ID 검증 정책은 [ACCOUNT_DELETION.md](ACCOUNT_DELETION.md)를 참고하세요.

## 카카오 선택 프로필 반영

최초 가입 시 `kakao_account.profile.nickname`과 `profile_image_url`(없으면 `thumbnail_image_url`)을 사용합니다. 닉네임/이미지의 동의가 필요하다는 플래그가 있거나 기본값 표시가 true이면 해당 항목을 사용하지 않고 FreLog 기본값으로 생성합니다. 이메일 확인을 거치는 가입에서도 선택 프로필 정보를 서버 세션에서 유지합니다.

사진은 계정 생성 트랜잭션이 성공한 후에만 가져와 기존 프로필 저장소에 PNG로 정규화해 저장합니다. 원격 URL은 DB에 저장하지 않습니다. 카카오 CDN 허용 호스트의 HTTPS만 사용하며 리다이렉트·내부 주소·5MB 초과 응답은 거절합니다. 기존 이미지 정규화의 해상도·픽셀 제한도 적용합니다. 다운로드나 이미지 처리가 실패해도 가입은 성공하고 기본 프로필을 사용합니다.

기존 계정으로 재로그인할 때는 FreLog에서 수정한 닉네임이나 사진을 덮어쓰지 않습니다. 사진 가져오는 동안 계정이 탈퇴된 경우 저장하지 않으며, 탈퇴 시 기존 이미지 삭제 정책도 유지합니다.

## Google·네이버 프로필과 DB 기록

Google OIDC의 `name`/`picture`, 네이버의 `response.nickname`(없으면 제공된 `name`)/`response.profile_image`도 최초 가입에 적용합니다. 이름은 `users.nickname`, 기존 공급자/회원 식별자는 `oauth_provider`/`oauth_id`, 이메일은 기존 검증 절차를 거쳐 `email`에 저장합니다. 생년월일·성별·연락처처럼 현재 서비스에 불필요한 필드는 추가 수집하지 않습니다.

이미지는 기존 카카오와 동일한 검증·다운로드·PNG 정규화 절차로 저장하고, DB `users.profile_image_url`에는 `/api/users/profile-images/{user_id}` 로컬 경로를 기록합니다. Google·네이버 CDN의 알려진 호스트도 허용 목록에 포함합니다. 사진을 직접 변경하거나 삭제하면 DB 경로도 함께 갱신하며, 탈퇴 시 경로를 NULL로 지웁니다. 외부 원본 URL이나 공급자 토큰은 DB에 저장하지 않습니다.

`mssql/migrations/2026-11-02-social-profile.sql`을 먼저 적용합니다. 기존 활성 계정의 프로필 파일이 있으면 해당 로컬 경로를 보완할 수 있습니다. 기존 계정의 닉네임이나 사용자가 수정한 사진은 로그인 시 덮어쓰지 않습니다.
