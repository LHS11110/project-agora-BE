package com.endpoint.frelog.global.exception;

import org.springframework.http.HttpStatus;

public enum ErrorCode {

    INVALID_CREDENTIALS(HttpStatus.UNAUTHORIZED, "AUTH_001", "이메일 또는 비밀번호가 올바르지 않습니다."),
    USER_SUSPENDED(HttpStatus.FORBIDDEN, "AUTH_002", "정지된 계정입니다. 관리자에게 문의하세요."),
    USER_WITHDRAWN(HttpStatus.FORBIDDEN, "AUTH_003", "탈퇴 처리된 계정입니다."),
    INVALID_TOKEN(HttpStatus.UNAUTHORIZED, "AUTH_004", "유효하지 않거나 만료된 인증 토큰입니다."),
    UNAUTHORIZED(HttpStatus.UNAUTHORIZED, "AUTH_005", "인증이 필요한 요청입니다."),
    ACCESS_DENIED(HttpStatus.FORBIDDEN, "AUTH_006", "접근 권한이 없습니다."),

    EMAIL_ALREADY_EXISTS(HttpStatus.CONFLICT, "USER_001", "이미 사용 중인 이메일입니다."),
    USER_NOT_FOUND(HttpStatus.NOT_FOUND, "USER_002", "사용자를 찾을 수 없습니다."),

    CANVAS_NOT_FOUND(HttpStatus.NOT_FOUND, "CANVAS_001", "캔버스를 찾을 수 없습니다."),
    CANVAS_ALREADY_EXISTS(HttpStatus.CONFLICT, "CANVAS_002", "이미 존재하는 캔버스입니다."),

    ELASTICSEARCH_INDEX_NOT_FOUND(HttpStatus.INTERNAL_SERVER_ERROR, "ES_001", "Elasticsearch 인덱스가 존재하지 않습니다."),

    INVALID_INPUT_VALUE(HttpStatus.BAD_REQUEST, "COMMON_001", "입력값이 유효하지 않습니다."),
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
