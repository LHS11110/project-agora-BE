package com.endpoint.frelog.domain.auth.email;
import com.endpoint.frelog.global.exception.*;
/** Expected failures commit attempt/send quotas; other failures roll back. */
public class EmailVerificationRejected extends CustomException {
 EmailVerificationRejected(ErrorCode code){super(code);}
}
