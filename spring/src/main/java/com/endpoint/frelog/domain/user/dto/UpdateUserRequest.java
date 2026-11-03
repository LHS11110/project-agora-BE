package com.endpoint.frelog.domain.user.dto;

import com.fasterxml.jackson.annotation.JsonProperty;
import jakarta.validation.constraints.Size;

public record UpdateUserRequest(
        @Size(min = 1, max = 100, message = "닉네임은 1자 이상 100자 이하여야 합니다.")
        @JsonProperty("nickname")
        String nickname,

        @Size(min = 12, max = 72, message = com.endpoint.frelog.domain.auth.validation.PasswordPolicy.MESSAGE)
        @JsonProperty("password")
        String password
) {
}
