package com.endpoint.frelog.domain.canvas.dto;

import com.endpoint.frelog.global.exception.ErrorCode;

/**
 * Separates Agora Canvas and IRADE Workspace storage keys, Elasticsearch indices,
 * JWT access tokens, and error semantics while sharing collaboration logic.
 */
public enum DocumentScope {
    CANVAS(
            "canvas",
            "canvas-id",
            "canvas-name",
            "canvas-access",
            "canvasId",
            "/api/canvases",
            ErrorCode.CANVAS_NOT_FOUND,
            ErrorCode.CANVAS_ACTIVE,
            ErrorCode.CANVAS_PASSWORD_REQUIRED,
            ErrorCode.CANVAS_PASSWORD_INVALID
    ),
    WORKSPACE(
            "workspace",
            "workspace-id",
            "workspace-name",
            "workspace-access",
            "workspaceId",
            "/api/irade/workspaces",
            ErrorCode.WORKSPACE_NOT_FOUND,
            ErrorCode.WORKSPACE_ACTIVE,
            ErrorCode.WORKSPACE_PASSWORD_REQUIRED,
            ErrorCode.WORKSPACE_PASSWORD_INVALID
    );

    private static final int WORKSPACE_RUNTIME_OFFSET = 1_000_000_000;

    private final String wireName;
    private final String idField;
    private final String nameField;
    private final String jwtSubject;
    private final String jwtIdClaim;
    private final String apiBasePath;
    private final ErrorCode notFoundError;
    private final ErrorCode activeError;
    private final ErrorCode passwordRequiredError;
    private final ErrorCode passwordInvalidError;

    DocumentScope(
            String wireName,
            String idField,
            String nameField,
            String jwtSubject,
            String jwtIdClaim,
            String apiBasePath,
            ErrorCode notFoundError,
            ErrorCode activeError,
            ErrorCode passwordRequiredError,
            ErrorCode passwordInvalidError
    ) {
        this.wireName = wireName;
        this.idField = idField;
        this.nameField = nameField;
        this.jwtSubject = jwtSubject;
        this.jwtIdClaim = jwtIdClaim;
        this.apiBasePath = apiBasePath;
        this.notFoundError = notFoundError;
        this.activeError = activeError;
        this.passwordRequiredError = passwordRequiredError;
        this.passwordInvalidError = passwordInvalidError;
    }

    public String wireName() { return wireName; }
    public String idField() { return idField; }
    public String nameField() { return nameField; }
    public String jwtSubject() { return jwtSubject; }
    public String jwtIdClaim() { return jwtIdClaim; }
    public String apiBasePath() { return apiBasePath; }
    public ErrorCode notFoundError() { return notFoundError; }
    public ErrorCode activeError() { return activeError; }
    public ErrorCode passwordRequiredError() { return passwordRequiredError; }
    public ErrorCode passwordInvalidError() { return passwordInvalidError; }

    public String redisKey(Integer id) {
        return wireName + ":" + id;
    }

    public String representativeImagePath(Integer id) {
        return apiBasePath + "/" + id + "/image";
    }

    /**
     * Maps workspace IDs into a non-overlapping positive 32-bit VM runtime namespace
     * while keeping canvas IDs unchanged for existing hypervisor directories.
     */
    public Integer runtimeTargetId(Integer id) {
        if (id == null || id <= 0) {
            return id;
        }
        if (this == CANVAS) {
            return id;
        }
        if (id > Integer.MAX_VALUE - WORKSPACE_RUNTIME_OFFSET) {
            throw new IllegalArgumentException("Workspace runtime id overflow: " + id);
        }
        return WORKSPACE_RUNTIME_OFFSET + id;
    }

    public static DocumentScope fromWire(String raw) {
        if (raw == null || raw.isBlank()) {
            return CANVAS;
        }
        if ("workspace".equalsIgnoreCase(raw.trim())) {
            return WORKSPACE;
        }
        if ("canvas".equalsIgnoreCase(raw.trim())) {
            return CANVAS;
        }
        throw new IllegalArgumentException("Unknown document scope: " + raw);
    }
}
