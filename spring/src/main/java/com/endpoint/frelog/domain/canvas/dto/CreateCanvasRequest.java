package com.endpoint.frelog.domain.canvas.dto;

import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

public record CreateCanvasRequest(
        @NotBlank(message = "캔버스 이름은 필수 입력값입니다.")
        @Size(max = 255, message = "캔버스 이름은 최대 255자까지 가능합니다.")
        String canvasName,

        Integer canvasId,

        Long userId
) {
    public CreateCanvasRequest(String canvasName, Integer canvasId) {
        this(canvasName, canvasId, null);
    }
}
