package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

public record CreateCanvasRequest(
        @NotBlank(message = "캔버스 이름은 필수 입력값입니다.")
        @Size(max = 255, message = "캔버스 이름은 최대 255자까지 가능합니다.")
        @JsonAlias({"canvas-name", "canvasName"})
        String canvasName,

        @JsonAlias({"canvas-id", "canvasId"})
        Integer canvasId,

        @JsonAlias({"user_id", "userId"})
        Long userId,

        @JsonAlias({"canvas-password", "canvasPassword"})
        String canvasPassword,

        @JsonAlias({"init-group", "initGroup"})
        String initGroup
) {
    public CreateCanvasRequest(String canvasName, Integer canvasId) {
        this(canvasName, canvasId, null, null, null);
    }

    public CreateCanvasRequest(String canvasName, Integer canvasId, Long userId) {
        this(canvasName, canvasId, userId, null, null);
    }
}
