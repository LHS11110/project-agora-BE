# 기기 승인 QR 로그인

로그인 화면에서 ‘QR 코드로 로그인’을 선택하면 새 기기에 QR과 6자리 확인 코드가 표시됩니다. 기존 로그인 기기의 카메라로 스캔하고, 요청 기기·IP와 계정을 확인한 뒤 새 기기의 확인 코드를 직접 입력하여 승인합니다. 별도 카메라 권한은 필요 없습니다. 스캔한 브라우저에 기존 로그인이 있어야 합니다.

기존 JWT를 QR에 넣거나 기기 사이에 복사하지 않습니다. QR에는 3분간 유효한 승인용 난수만 URL fragment로 담고, 페이지 로드 후 주소에서 제거합니다. 새 기기는 별도 polling secret, Secure/HttpOnly/SameSite=Strict 쿠키, 요청 IP를 모두 증명해야 결과를 받을 수 있습니다. 서버는 난수 해시만 저장합니다. 요청 시작은 사용자 동작으로만 수행하고 IP당 분당 6회로 제한합니다. 확인 코드를 5번 틀리면 거절 처리합니다.

기존 기기는 인증된 POST /api/auth/qr/preview, /decide를 사용합니다. /start, /poll, /cancel은 새 기기용 공개 경로이며 모든 요청에서 고정 HTTPS Origin을 검증합니다. 승인·취소·수신은 DB 행 잠금으로 직렬화합니다. 성공 수신 시 USED로 바꾸고 사용자 연결을 제거하며, 새 기기의 IP로 새 JWT를 발급합니다. 승인한 기기의 현재 MFA 버전을 계승하되 수신 전에 MFA가 재설정되면 거부합니다. 계정 탈퇴 시 사용자와 연결된 QR 승인 요청도 삭제합니다. 만료된 요청은 매분 정리합니다.

설정은 app.passkeys.origin과 동일한 HTTPS origin을 사용합니다. DB 배포 전에 mssql/migrations/2026-11-02-qr-login.sql을 적용해야 합니다. 기본값은 https://frelog.p-e.kr입니다. OAuth 공급자의 device grant가 아닌 FreLog 내부의 기기 승인 기능입니다.

검증: QrLoginIntegrationTest (승인, 일회성, 서로 다른 기기 IP, 쿠키/비밀 분리, 만료, 거절/취소, 실패 횟수, Origin, 인증 필터, MFA 변경). tests/integration/qr-login.mjs는 두 브라우저에서 승인 UI와 로그인 수신을 검증합니다.
