package com.endpoint.frelog.domain.auth.dto;

import jakarta.validation.constraints.Email;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.Size;

public record SignupRequest(
        @NotBlank(message = "이메일은 필수 입력값입니다.")
        @Email(message = "올바른 이메일 형식이 아닙니다.")
        @Size(max = 255, message = "이메일은 최대 255자까지 가능합니다.")
        String email,

        @NotBlank(message = "비밀번호는 필수 입력값입니다.")
        @Size(min = 12, max = 72, message = com.endpoint.frelog.domain.auth.validation.PasswordPolicy.MESSAGE)
        String password,

        @NotBlank(message = "닉네임은 필수 입력값입니다.")
        @Size(max = 100, message = "닉네임은 최대 100자까지 가능합니다.")
        String nickname,

        @NotBlank(message = "이메일 인증을 완료해주세요.")
        @Size(max = 128)
        String emailVerificationToken
) {
    public SignupRequest(String email,String password,String nickname){this(email,password,nickname,null);}
}
