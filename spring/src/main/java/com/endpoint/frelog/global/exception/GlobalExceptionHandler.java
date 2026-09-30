package com.endpoint.frelog.global.exception;

import org.springframework.http.ResponseEntity;
import org.springframework.security.access.AccessDeniedException;
import org.springframework.security.authentication.BadCredentialsException;
import org.springframework.security.core.AuthenticationException;
import org.springframework.validation.BindException;
import org.springframework.web.bind.MethodArgumentNotValidException;
import org.springframework.web.method.annotation.MethodArgumentTypeMismatchException;
import org.springframework.http.converter.HttpMessageNotReadableException;
import org.springframework.web.HttpMediaTypeNotSupportedException;
import org.springframework.web.HttpMediaTypeNotAcceptableException;
import org.springframework.web.HttpRequestMethodNotSupportedException;
import org.springframework.web.bind.MissingRequestHeaderException;
import org.springframework.web.bind.MissingServletRequestParameterException;
import org.springframework.web.multipart.support.MissingServletRequestPartException;
import org.springframework.web.servlet.resource.NoResourceFoundException;
import org.springframework.web.multipart.MaxUploadSizeExceededException;
import org.springframework.web.multipart.MultipartException;
import org.springframework.web.bind.annotation.ExceptionHandler;
import org.springframework.web.bind.annotation.RestControllerAdvice;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.slf4j.MDC;
import org.springframework.web.context.request.RequestContextHolder;
import org.springframework.web.context.request.ServletRequestAttributes;
import jakarta.servlet.http.HttpServletRequest;
import com.endpoint.frelog.global.logging.RequestLogAttributes;

import java.util.List;

@RestControllerAdvice
public class GlobalExceptionHandler {

    private static final Logger log = LoggerFactory.getLogger(GlobalExceptionHandler.class);

    @ExceptionHandler(CustomException.class)
    public ResponseEntity<ErrorResponse> handleCustomException(CustomException ex) {
        ErrorCode errorCode = ex.getErrorCode();
        recordFailure(errorCode, ex);
        if (errorCode.getHttpStatus().is5xxServerError()) {
            return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
        }
        ErrorResponse response = new ErrorResponse(errorCode, ex.getMessage());
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(BadCredentialsException.class)
    public ResponseEntity<ErrorResponse> handleBadCredentialsException(BadCredentialsException ex) {
        ErrorCode errorCode = ErrorCode.INVALID_CREDENTIALS;
        recordFailure(errorCode, ex);
        ErrorResponse response = new ErrorResponse(errorCode);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(AuthenticationException.class)
    public ResponseEntity<ErrorResponse> handleAuthenticationException(AuthenticationException ex) {
        ErrorCode errorCode = ErrorCode.UNAUTHORIZED;
        recordFailure(errorCode, ex);
        ErrorResponse response = new ErrorResponse(errorCode);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(AccessDeniedException.class)
    public ResponseEntity<ErrorResponse> handleAccessDeniedException(AccessDeniedException ex) {
        ErrorCode errorCode = ErrorCode.ACCESS_DENIED;
        recordFailure(errorCode, ex);
        ErrorResponse response = new ErrorResponse(errorCode);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(MethodArgumentNotValidException.class)
    public ResponseEntity<ErrorResponse> handleMethodArgumentNotValid(MethodArgumentNotValidException ex) {
        List<ErrorResponse.FieldErrorDetail> fieldErrors = ex.getBindingResult().getFieldErrors().stream()
                .map(error -> new ErrorResponse.FieldErrorDetail(
                        error.getField(),
                        "",
                        error.getDefaultMessage()
                ))
                .toList();

        ErrorCode errorCode = ErrorCode.INVALID_INPUT_VALUE;
        recordFailure(errorCode, ex);
        ErrorResponse response = new ErrorResponse(errorCode, fieldErrors);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(BindException.class)
    public ResponseEntity<ErrorResponse> handleBindException(BindException ex) {
        List<ErrorResponse.FieldErrorDetail> fieldErrors = ex.getBindingResult().getFieldErrors().stream()
                .map(error -> new ErrorResponse.FieldErrorDetail(
                        error.getField(),
                        "",
                        error.getDefaultMessage()
                ))
                .toList();

        ErrorCode errorCode = ErrorCode.INVALID_INPUT_VALUE;
        recordFailure(errorCode, ex);
        ErrorResponse response = new ErrorResponse(errorCode, fieldErrors);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(response);
    }

    @ExceptionHandler(Exception.class)
    public ResponseEntity<ErrorResponse> handleGenericException(Exception ex) {
        ErrorCode errorCode = ErrorCode.INTERNAL_SERVER_ERROR;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler({
            HttpMessageNotReadableException.class,
            MethodArgumentTypeMismatchException.class,
            MissingRequestHeaderException.class,
            MissingServletRequestParameterException.class,
            MissingServletRequestPartException.class
    })
    public ResponseEntity<ErrorResponse> handleMalformedRequest(Exception ex) {
        ErrorCode errorCode = ErrorCode.INVALID_INPUT_VALUE;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(HttpRequestMethodNotSupportedException.class)
    public ResponseEntity<ErrorResponse> handleMethodNotAllowed(HttpRequestMethodNotSupportedException ex) {
        ErrorCode errorCode = ErrorCode.METHOD_NOT_ALLOWED;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(NoResourceFoundException.class)
    public ResponseEntity<ErrorResponse> handleResourceNotFound(NoResourceFoundException ex) {
        ErrorCode errorCode = ErrorCode.RESOURCE_NOT_FOUND;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(HttpMediaTypeNotAcceptableException.class)
    public ResponseEntity<ErrorResponse> handleNotAcceptable(HttpMediaTypeNotAcceptableException ex) {
        ErrorCode errorCode = ErrorCode.NOT_ACCEPTABLE;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(MaxUploadSizeExceededException.class)
    public ResponseEntity<ErrorResponse> handleUploadTooLarge(MaxUploadSizeExceededException ex) {
        ErrorCode errorCode = ErrorCode.PAYLOAD_TOO_LARGE;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(MultipartException.class)
    public ResponseEntity<ErrorResponse> handleMultipartException(MultipartException ex) {
        for (Throwable cause = ex; cause != null; cause = cause.getCause()) {
            if (cause instanceof MaxUploadSizeExceededException) {
                ErrorCode errorCode = ErrorCode.PAYLOAD_TOO_LARGE;
                recordFailure(errorCode, ex);
                return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
            }
        }
        ErrorCode errorCode = ErrorCode.INVALID_INPUT_VALUE;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    @ExceptionHandler(HttpMediaTypeNotSupportedException.class)
    public ResponseEntity<ErrorResponse> handleUnsupportedMediaType(HttpMediaTypeNotSupportedException ex) {
        ErrorCode errorCode = ErrorCode.UNSUPPORTED_MEDIA_TYPE;
        recordFailure(errorCode, ex);
        return ResponseEntity.status(errorCode.getHttpStatus()).body(new ErrorResponse(errorCode));
    }

    private void recordFailure(ErrorCode errorCode, Throwable cause) {
        ServletRequestAttributes attributes = (ServletRequestAttributes) RequestContextHolder.getRequestAttributes();
        HttpServletRequest request = attributes == null ? null : attributes.getRequest();
        RequestLogAttributes.markFailure(request, errorCode.getCode(), cause);

        String previousCode = MDC.get("error_code");
        String previousType = MDC.get("error_type");
        try {
            MDC.put("error_code", errorCode.getCode());
            if (cause != null) MDC.put("error_type", cause.getClass().getSimpleName());
            if (errorCode.getHttpStatus().is5xxServerError()) {
                log.error("HTTP request failed: error_code={}", errorCode.getCode(), cause);
            } else {
                log.warn("HTTP request rejected: error_code={}, error_type={}",
                        errorCode.getCode(), cause == null ? "unknown" : cause.getClass().getSimpleName());
            }
        } finally {
            if (previousCode == null) MDC.remove("error_code"); else MDC.put("error_code", previousCode);
            if (previousType == null) MDC.remove("error_type"); else MDC.put("error_type", previousType);
        }
    }
}
