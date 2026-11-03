package com.endpoint.frelog.domain.auth.mfa;
import com.endpoint.frelog.global.exception.*;
/** Expected rejection: attempt counters must commit even when verification fails. */
public class MfaRejected extends CustomException {
    MfaRejected(String message){super(ErrorCode.INVALID_INPUT_VALUE,message);}
}
