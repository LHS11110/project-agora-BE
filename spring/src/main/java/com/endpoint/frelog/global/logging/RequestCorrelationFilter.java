package com.endpoint.frelog.global.logging;

import jakarta.servlet.FilterChain;
import jakarta.servlet.ServletException;
import jakarta.servlet.http.HttpServletRequest;
import jakarta.servlet.http.HttpServletResponse;
import org.slf4j.MDC;
import org.springframework.core.Ordered;
import org.springframework.core.annotation.Order;
import org.springframework.stereotype.Component;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.util.StringUtils;
import org.springframework.web.filter.OncePerRequestFilter;

import java.io.IOException;
import java.util.LinkedHashMap;
import java.util.Map;
import java.util.HashMap;
import java.util.UUID;
import java.util.regex.Pattern;

/** Adds a safe request ID to HTTP responses, MDC, and the operational log stream. */
@Component
@Order(Ordered.HIGHEST_PRECEDENCE)
public class RequestCorrelationFilter extends OncePerRequestFilter {
    public static final String HEADER = "X-Request-ID";
    private static final Pattern SAFE_REQUEST_ID = Pattern.compile("[A-Za-z0-9._:-]{1,64}");

    private final ElasticsearchBulkLogService logService;

    @Value("${app.environment:local}")
    private String environment;

    @Value("${app.version:unknown}")
    private String version;

    public RequestCorrelationFilter(ElasticsearchBulkLogService logService) {
        this.logService = logService;
    }

    @Override
    protected boolean shouldNotFilter(HttpServletRequest request) {
        return !request.getRequestURI().startsWith("/api/");
    }

    @Override
    protected void doFilterInternal(HttpServletRequest request, HttpServletResponse response,
                                    FilterChain filterChain) throws ServletException, IOException {
        String supplied = request.getHeader(HEADER);
        String requestId = StringUtils.hasText(supplied) && SAFE_REQUEST_ID.matcher(supplied).matches()
                ? supplied : UUID.randomUUID().toString();
        String operation = request.getMethod() + " " + request.getRequestURI();
        long startedAt = System.nanoTime();
        Map<String, String> previousMdc = MDC.getCopyOfContextMap();

        request.setAttribute(HEADER, requestId);
        response.setHeader(HEADER, requestId);
        MDC.put("request_id", requestId);
        MDC.put("operation", operation);
        MDC.put("outcome", "in_progress");
        MDC.put("environment", environment == null || environment.isBlank() ? "local" : environment);
        MDC.put("version", version == null || version.isBlank() ? "unknown" : version);

        try {
            filterChain.doFilter(request, response);
        } catch (IOException | ServletException exception) {
            RequestLogAttributes.markFailure(request, "HTTP_PIPELINE_FAILED", exception);
            if (response.getStatus() < 500) response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            throw exception;
        } catch (RuntimeException exception) {
            RequestLogAttributes.markFailure(request, "HTTP_PIPELINE_FAILED", exception);
            if (response.getStatus() < 500) response.setStatus(HttpServletResponse.SC_INTERNAL_SERVER_ERROR);
            throw exception;
        } finally {
            int status = response.getStatus();
            String outcome = status >= 500 ? "failure" : status >= 400 ? "rejected" : "success";
            String level = status >= 500 ? "ERROR" : status >= 400 ? "WARN" : "INFO";
            Object errorCode = request.getAttribute(RequestLogAttributes.ERROR_CODE);
            if (errorCode == null && status >= 400) errorCode = "HTTP_" + status;
            Object errorType = request.getAttribute(RequestLogAttributes.ERROR_TYPE);
            Object errorMessage = request.getAttribute(RequestLogAttributes.ERROR_MESSAGE);

            Map<String, Object> details = new LinkedHashMap<>();
            details.put("http_method", request.getMethod());
            details.put("path", request.getRequestURI()); // Deliberately excludes query strings and tokens.
            details.put("http_status", status);
            details.put("duration_ms", (System.nanoTime() - startedAt) / 1_000_000L);
            if (errorCode != null) details.put("error_code", errorCode);
            if (errorType != null) details.put("error_type", errorType);
            if (errorMessage != null) details.put("error_message", errorMessage);

            MDC.put("outcome", outcome);
            if (errorCode != null) MDC.put("error_code", errorCode.toString());
            MDC.put("http_status", Integer.toString(status));
            try {
                logService.record("http", "http_request", level,
                        status >= 400 ? "HTTP request completed with failure" : "HTTP request completed",
                        details);
            } finally {
                if (previousMdc == null) MDC.clear();
                else MDC.setContextMap(new HashMap<>(previousMdc));
            }
        }
    }
}
