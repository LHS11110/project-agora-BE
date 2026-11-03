package com.endpoint.frelog.domain.auth.qr;
import com.endpoint.frelog.global.exception.*;
public class QrRejected extends CustomException {
 QrRejected(String message){super(ErrorCode.INVALID_INPUT_VALUE,message);}
}
