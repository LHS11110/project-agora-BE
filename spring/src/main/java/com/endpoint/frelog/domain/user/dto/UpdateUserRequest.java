package com.endpoint.frelog.domain.user.dto;

import com.fasterxml.jackson.annotation.JsonProperty;
import jakarta.validation.constraints.Size;

public record UpdateUserRequest(
        @Size(min = 1, max = 100, message = "닉네임은 1자 이상 100자 이하여야 합니다.")
        @JsonProperty("nickname")
        String nickname,

        @Size(min = 4, max = 100, message = "비밀번호는 4자 이상이어야 합니다.")
        @JsonProperty("password")
        String password
) {
}
