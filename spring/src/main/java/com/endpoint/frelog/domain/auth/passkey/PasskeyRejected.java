package com.endpoint.frelog.domain.auth.passkey;
import com.endpoint.frelog.global.exception.*;
public class PasskeyRejected extends CustomException {
    PasskeyRejected(String message) { super(ErrorCode.INVALID_INPUT_VALUE,message); }
}
