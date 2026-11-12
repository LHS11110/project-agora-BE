package com.endpoint.frelog.global.exception;

import org.springframework.http.HttpStatus;

public enum ErrorCode {
    EMAIL_VERIFICATION_REQUIRED(HttpStatus.BAD_REQUEST, "EMAIL_001", "이메일 인증을 완료해주세요."),
    EMAIL_VERIFICATION_INVALID(HttpStatus.BAD_REQUEST, "EMAIL_002", "인증 코드가 올바르지 않거나 만료되었습니다. 다시 확인해주세요."),
    EMAIL_VERIFICATION_RATE_LIMIT(HttpStatus.TOO_MANY_REQUESTS, "EMAIL_003", "인증 요청이 너무 많습니다. 잠시 후 다시 시도해주세요."),
    EMAIL_DELIVERY_UNAVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "EMAIL_004", "인증 메일을 보낼 수 없습니다. 잠시 후 다시 시도하거나 Google로 계속해주세요."),

    INVALID_CREDENTIALS(HttpStatus.UNAUTHORIZED, "AUTH_001", "이메일 또는 비밀번호가 올바르지 않습니다."),
    USER_SUSPENDED(HttpStatus.FORBIDDEN, "AUTH_002", "정지된 계정입니다. 관리자에게 문의하세요."),
    USER_WITHDRAWN(HttpStatus.FORBIDDEN, "AUTH_003", "탈퇴 처리된 계정입니다."),
    INVALID_TOKEN(HttpStatus.UNAUTHORIZED, "AUTH_004", "유효하지 않거나 만료된 인증 토큰입니다."),
    UNAUTHORIZED(HttpStatus.UNAUTHORIZED, "AUTH_005", "인증이 필요한 요청입니다."),
    ACCESS_DENIED(HttpStatus.FORBIDDEN, "AUTH_006", "접근 권한이 없습니다."),


    EMAIL_ALREADY_EXISTS(HttpStatus.CONFLICT, "USER_001", "이미 존재하는 계정입니다. 기존 로그인 방식을 사용해주세요."),
    USER_NOT_FOUND(HttpStatus.NOT_FOUND, "USER_002", "사용자를 찾을 수 없습니다."),
    USER_ACTIVE(HttpStatus.CONFLICT, "USER_003", "캔버스를 이용 중인 사용자는 이 작업을 수행할 수 없습니다."),

    CANVAS_NOT_FOUND(HttpStatus.NOT_FOUND, "CANVAS_001", "캔버스를 찾을 수 없습니다."),
    CANVAS_ALREADY_EXISTS(HttpStatus.CONFLICT, "CANVAS_002", "이미 존재하는 캔버스입니다."),
    ALREADY_CONNECTED(HttpStatus.CONFLICT, "CANVAS_003", "이미 캔버스에 접속 중입니다."),
    CANVAS_PASSWORD_REQUIRED(HttpStatus.FORBIDDEN, "CANVAS_004", "캔버스 비밀번호가 필요합니다."),
    CANVAS_PASSWORD_INVALID(HttpStatus.FORBIDDEN, "CANVAS_005", "캔버스 비밀번호가 올바르지 않습니다."),
    CANVAS_ACTIVE(HttpStatus.CONFLICT, "CANVAS_006", "캔버스가 활성 상태입니다. 해당 캔버스에 접속한 뒤 캔버스 설정에서 변경해 주세요."),

    ELASTICSEARCH_INDEX_NOT_FOUND(HttpStatus.INTERNAL_SERVER_ERROR, "ES_001", "Elasticsearch 인덱스가 존재하지 않습니다."),

    NO_SERVER_AVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "LB_001", "등록된 서버가 없습니다."),
    NO_REDIS_AVAILABLE(HttpStatus.NOT_FOUND, "LB_002", "등록된 Redis 서버가 없습니다."),
    SERVER_NOT_FOUND(HttpStatus.NOT_FOUND, "LB_003", "서버를 찾을 수 없습니다."),
    REDIS_NOT_FOUND(HttpStatus.NOT_FOUND, "LB_004", "Redis 서버를 찾을 수 없습니다."),

    RUNTIME_MANAGER_UNAVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "VM_001", "캔버스 실행환경 관리자에 연결할 수 없습니다. VM 서비스 연결 상태를 확인한 뒤 다시 시도해주세요."),
    RUNTIME_VM_UNAVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "VM_002", "이 캔버스의 가상머신을 시작하거나 사용할 수 없습니다. 호스트 리소스와 VM 상태를 확인한 뒤 다시 시도해주세요."),
    RUNTIME_CPP_SERVER_UNAVAILABLE(HttpStatus.SERVICE_UNAVAILABLE, "VM_003", "캔버스에 할당된 C++ 브로커를 찾거나 실행 요청을 전달할 수 없습니다. 캔버스 연결 상태를 확인한 뒤 다시 시도해주세요."),

    BAD_REQUEST(HttpStatus.BAD_REQUEST, "COMMON_000", "잘못된 요청입니다."),
    FORBIDDEN(HttpStatus.FORBIDDEN, "AUTH_007", "권한이 없습니다."),
    INVALID_INPUT_VALUE(HttpStatus.BAD_REQUEST, "COMMON_001", "입력값이 유효하지 않습니다."),
    UNSUPPORTED_MEDIA_TYPE(HttpStatus.UNSUPPORTED_MEDIA_TYPE, "COMMON_004", "지원하지 않는 콘텐츠 형식입니다."),
    PAYLOAD_TOO_LARGE(HttpStatus.CONTENT_TOO_LARGE, "COMMON_003", "요청 크기가 허용된 한도를 초과했습니다."),
    METHOD_NOT_ALLOWED(HttpStatus.METHOD_NOT_ALLOWED, "COMMON_005", "지원하지 않는 HTTP 메서드입니다."),
    RESOURCE_NOT_FOUND(HttpStatus.NOT_FOUND, "COMMON_006", "요청한 경로를 찾을 수 없습니다."),
    NOT_ACCEPTABLE(HttpStatus.NOT_ACCEPTABLE, "COMMON_007", "요청한 응답 형식을 제공할 수 없습니다."),
    INTERNAL_SERVER_ERROR(HttpStatus.INTERNAL_SERVER_ERROR, "COMMON_002", "서버 내부 오류가 발생했습니다.");

    private final HttpStatus httpStatus;
    private final String code;
    private final String message;

    ErrorCode(HttpStatus httpStatus, String code, String message) {
        this.httpStatus = httpStatus;
        this.code = code;
        this.message = message;
    }

    public HttpStatus getHttpStatus() {
        return httpStatus;
    }

    public String getCode() {
        return code;
    }

    public String getMessage() {
        return message;
    }
}
