package com.endpoint.frelog.domain.loadbalancer.dto;

import jakarta.validation.constraints.NotBlank;

public record RegisterServerRequest(
        @NotBlank(message = "서버 IP는 필수입니다.")
        String serverIp,

        @NotBlank(message = "서버 포트는 필수입니다.")
        String serverPort,

        String serverName
) {
}
