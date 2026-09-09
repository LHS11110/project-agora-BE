# Project Agora BE (Backend)

`Project Agora`의 백엔드 서비스 저장소입니다.  
`~/github/project-agora-DB`의 MS SQL Server 데이터베이스 스키마와 연동되어 회원 관리 및 JWT 기반 인증 기능을 제공합니다.

---

## 1. 데이터베이스(MSSQL) 접속 설정 가이드

요구사항에 따라 **데이터베이스 접속 호스트(IP)와 포트를 별도로 손쉽게 관리**할 수 있도록 분리 구성되어 있습니다.

### 설정 파일 위치
- [application.properties](file:///home/ubuntu/github/project-agora-BE/spring/src/main/resources/application.properties)

### 관리 프로퍼티 및 환경변수 매핑

| 항목 | 설정 프로퍼티 (`application.properties`) | 환경 변수 (Environment Variable) | 기본값 |
| :--- | :--- | :--- | :--- |
| **DB IP (호스트)** | `app.db.host` | `DB_HOST` | `127.0.0.1` |
| **DB 포트** | `app.db.port` | `DB_PORT` | `1433` |
| **DB 이름** | `app.db.name` | `DB_NAME` | `agora_db` |
| **DB 사용자** | `app.db.username` | `DB_USER` | `agora_user` |
| **DB 비밀번호** | `app.db.password` | `DB_PASSWORD` | `AgoraUserSecret@Passw0rd!2026` |

Spring DataSource URL은 위 분리된 프로퍼티를 조합하여 자동 구성됩니다:
```properties
spring.datasource.url=jdbc:sqlserver://${app.db.host}:${app.db.port};databaseName=${app.db.name};encrypt=false;trustServerCertificate=true
```

### 실행 시 환경 변수로 IP/포트 변경 예시
```bash
# 원격 또는 다른 IP/Port의 MSSQL 서버로 실행
DB_HOST=192.168.1.100 DB_PORT=14333 ./gradlew bootRun
```

---

## 2. 데이터베이스 스키마 (`users` 테이블)

`project-agora-DB`의 스키마와 1:1로 매핑되는 [User](file:///home/ubuntu/github/project-agora-BE/spring/src/main/java/com/endpoint/frelog/domain/user/entity/User.java) 엔티티가 구현되어 있습니다.

- `user_id` (PK, IDENTITY)
- `email` (NVARCHAR(255), UNIQUE, NOT NULL)
- `password_hash` (NVARCHAR(255), BCrypt 암호화 저장)
- `nickname` (NVARCHAR(100), NOT NULL)
- `role` (VARCHAR(20), `ROLE_USER`, `ROLE_ADMIN`)
- `status` (VARCHAR(20), `ACTIVE`, `SUSPENDED`, `WITHDRAWN`)
- `oauth_provider` / `oauth_id` (NVARCHAR, 소셜 로그인 연동 대비)
- `last_login_at` (DATETIME2, 로그인 성공 시 자동 갱신)
- `created_at` / `updated_at` (DATETIME2, 자동 감사)

---

## 3. 인증 API 명세

### (1) 회원가입 (`POST /api/auth/signup`)
- **Request Body**:
  ```json
  {
    "email": "user@agora.com",
    "password": "password1234",
    "nickname": "아고라유저"
  }
  ```
- **Response (201 Created)**:
  ```json
  {
    "userId": 1,
    "email": "user@agora.com",
    "nickname": "아고라유저",
    "role": "ROLE_USER",
    "status": "ACTIVE",
    "lastLoginAt": null,
    "createdAt": "2026-09-09T13:30:00"
  }
  ```

### (2) 로그인 (`POST /api/auth/login`)
- **Request Body**:
  ```json
  {
    "email": "user@agora.com",
    "password": "password1234"
  }
  ```
- **Response (200 OK)**:
  ```json
  {
    "tokenType": "Bearer",
    "accessToken": "eyJhbGciOiJIUzI1NiJ9...",
    "user": {
      "userId": 1,
      "email": "user@agora.com",
      "nickname": "아고라유저",
      "role": "ROLE_USER",
      "status": "ACTIVE",
      "lastLoginAt": "2026-09-09T13:35:00",
      "createdAt": "2026-09-09T13:30:00"
    }
  }
  ```
- **예외 응답**:
  - 비밀번호 불일치 / 사용자 미존재: `401 Unauthorized` (`AUTH_001`)
  - 정지 계정 (`SUSPENDED`): `403 Forbidden` (`AUTH_002`)
  - 탈퇴 계정 (`WITHDRAWN`): `403 Forbidden` (`AUTH_003`)

### (3) 내 정보 조회 (`GET /api/auth/me`)
- **Header**: `Authorization: Bearer <accessToken>`
- **Response (200 OK)**:
  ```json
  {
    "userId": 1,
    "email": "user@agora.com",
    "nickname": "아고라유저",
    "role": "ROLE_USER",
    "status": "ACTIVE",
    "lastLoginAt": "2026-09-09T13:35:00",
    "createdAt": "2026-09-09T13:30:00"
  }
  ```

### (4) 헬스체크 (`GET /api/auth/health`)
- 인증 없이 접근 가능 (200 OK)

---

## 4. 빌드 및 테스트 실행

```bash
cd spring

# 테스트 전체 실행 (18개 테스트 통과)
./gradlew test

# 실행 가능한 jar 빌드
./gradlew bootJar

# 애플리케이션 실행
./gradlew bootRun
```
