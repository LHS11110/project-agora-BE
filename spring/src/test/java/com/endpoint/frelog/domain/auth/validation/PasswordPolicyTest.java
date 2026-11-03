package com.endpoint.frelog.domain.auth.validation;

import org.junit.jupiter.params.ParameterizedTest;
import org.junit.jupiter.params.provider.NullAndEmptySource;
import org.junit.jupiter.params.provider.ValueSource;
import static org.assertj.core.api.Assertions.*;
import com.endpoint.frelog.global.exception.CustomException;

class PasswordPolicyTest {
    @ParameterizedTest
    @NullAndEmptySource
    @ValueSource(strings={"Short1!", "abcdefghijkl", "123456789012!", "Letters123456", "Password12! ", "Password12!\n", "Password12!\u00a0", "Password12!\u200b"})
    void rejectsWeakOrWhitespacePasswords(String value) {
        assertThatThrownBy(() -> PasswordPolicy.validate(value)).isInstanceOf(CustomException.class);
    }
    @ParameterizedTest
    @ValueSource(strings={"Password123!", "lowercase12!", "ONLYUPPER12!", "가나다라마바사아자차12!", "hyphen-only12", "brackets123[]"})
    void acceptsLettersDigitsAndSymbols(String value) { assertThatCode(() -> PasswordPolicy.validate(value)).doesNotThrowAnyException(); }
    @org.junit.jupiter.api.Test void respectsBcryptByteBoundary() {
        assertThatCode(() -> PasswordPolicy.validate("a".repeat(70)+"1!")).doesNotThrowAnyException();
        assertThatThrownBy(() -> PasswordPolicy.validate("a".repeat(71)+"1!")).isInstanceOf(CustomException.class);
        assertThatThrownBy(() -> PasswordPolicy.validate("한".repeat(24)+"1!")).isInstanceOf(CustomException.class);
    }
}
