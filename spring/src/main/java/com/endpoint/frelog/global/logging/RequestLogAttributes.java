package com.endpoint.frelog.global.logging;

import jakarta.servlet.http.HttpServletRequest;

/** Request-scoped failure details consumed by RequestCorrelationFilter. */
public final class RequestLogAttributes {
    public static final String ERROR_CODE = RequestLogAttributes.class.getName() + ".errorCode";
    public static final String ERROR_TYPE = RequestLogAttributes.class.getName() + ".errorType";
    public static final String ERROR_MESSAGE = RequestLogAttributes.class.getName() + ".errorMessage";

    private RequestLogAttributes() {}

    public static void markFailure(HttpServletRequest request, String code, Throwable cause) {
        if (request == null) return;
        request.setAttribute(ERROR_CODE, code);
        if (cause != null) {
            request.setAttribute(ERROR_TYPE, cause.getClass().getSimpleName());
        }
    }
}
