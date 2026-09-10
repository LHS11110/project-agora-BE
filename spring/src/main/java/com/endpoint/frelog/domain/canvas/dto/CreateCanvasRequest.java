package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

/**
 * 캔버스 생성 요청 DTO
 * 캔버스 생성 시 캔버스 이름, 캔버스 비밀번호, 초기 그룹만 설정 가능
 * - canvas_id는 MS SQL에서 자동 생성 (maxId + 1)
 * - 소유자(user_id)는 인증된 사용자 세션에서 자동 결정
 */
@JsonIgnoreProperties(ignoreUnknown = true)
public record CreateCanvasRequest(
        @NotBlank(message = "캔버스 이름은 필수 입력값입니다.")
        @Size(max = 255, message = "캔버스 이름은 최대 255자까지 가능합니다.")
        @JsonAlias({"canvas-name", "canvasName"})
        String canvasName,

        @JsonAlias({"canvas-password", "canvasPassword"})
        String canvasPassword,

        @JsonAlias({"init-group", "initGroup"})
        String initGroup
) {
    public CreateCanvasRequest(String canvasName) {
        this(canvasName, null, null);
    }
}
