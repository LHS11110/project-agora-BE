package com.endpoint.frelog.domain.auth.validation;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import java.nio.charset.StandardCharsets;

/** Shared by account creation and password changes; existing login credentials remain valid. */
public final class PasswordPolicy {
    public static final String MESSAGE = "비밀번호는 12자 이상이며 문자·숫자·특수문자를 포함해야 합니다. 공백은 사용할 수 없으며 UTF-8 기준 72바이트 이하여야 합니다.";
    private PasswordPolicy() {}

    public static void validate(String password) {
        if (password == null || password.codePointCount(0, password.length()) < 12
                || password.getBytes(StandardCharsets.UTF_8).length > 72) reject();
        boolean letter = false, digit = false, symbol = false;
        for (int point : password.codePoints().toArray()) {
            if (Character.isWhitespace(point) || Character.isSpaceChar(point)
                    || Character.isISOControl(point) || Character.getType(point) == Character.FORMAT
                    || Character.getType(point) == Character.SURROGATE) reject();
            if (Character.isLetter(point)) letter = true;
            else if (Character.isDigit(point)) digit = true;
            else {
                int type = Character.getType(point);
                if (switch (type) {
                    case Character.CONNECTOR_PUNCTUATION, Character.DASH_PUNCTUATION,
                         Character.START_PUNCTUATION, Character.END_PUNCTUATION,
                         Character.INITIAL_QUOTE_PUNCTUATION, Character.FINAL_QUOTE_PUNCTUATION,
                         Character.OTHER_PUNCTUATION, Character.MATH_SYMBOL, Character.CURRENCY_SYMBOL,
                         Character.MODIFIER_SYMBOL, Character.OTHER_SYMBOL -> true;
                    default -> false;
                }) symbol = true;
            }
        }
        if (!letter || !digit || !symbol) reject();
    }

    private static void reject() { throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, MESSAGE); }
}
