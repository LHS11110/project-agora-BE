package com.endpoint.frelog.domain.auth.mfa;

import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;
import java.nio.ByteBuffer;
import java.security.MessageDigest;
import java.nio.charset.StandardCharsets;

/** RFC 6238, Google Authenticator defaults: SHA-1, six digits, 30 seconds. */
public final class Totp {
    private static final String ALPHABET="ABCDEFGHIJKLMNOPQRSTUVWXYZ234567";
    private Totp() {}
    public static String base32(byte[] value) {
        StringBuilder out=new StringBuilder();int buffer=0,bits=0;
        for(byte b:value){buffer=(buffer<<8)|(b&255);bits+=8;while(bits>=5){bits-=5;out.append(ALPHABET.charAt((buffer>>bits)&31));}}
        if(bits>0)out.append(ALPHABET.charAt((buffer<<(5-bits))&31));return out.toString();
    }
    public static byte[] decode(String value) {
        var out=new java.io.ByteArrayOutputStream();int buffer=0,bits=0;
        for(char c:value.toCharArray()){int digit=ALPHABET.indexOf(c);if(digit<0)throw new IllegalArgumentException("Invalid base32");buffer=(buffer<<5)|digit;bits+=5;if(bits>=8){bits-=8;out.write((buffer>>bits)&255);}}return out.toByteArray();
    }
    public static String code(String secret,long step) {
        try{Mac mac=Mac.getInstance("HmacSHA1");mac.init(new SecretKeySpec(decode(secret),"HmacSHA1"));byte[] hash=mac.doFinal(ByteBuffer.allocate(8).putLong(step).array());int offset=hash[hash.length-1]&15;int number=ByteBuffer.wrap(hash,offset,4).getInt()&0x7fffffff;return String.format(java.util.Locale.ROOT,"%06d",number%1_000_000);}catch(java.security.GeneralSecurityException e){throw new IllegalStateException(e);}
    }
    public static long match(String secret,String input,long current,long lastUsed) {
        if(input==null||!input.matches("[0-9]{6}"))return -1;
        for(long step=current-1;step<=current+1;step++)if(step>lastUsed&&MessageDigest.isEqual(code(secret,step).getBytes(StandardCharsets.US_ASCII),input.getBytes(StandardCharsets.US_ASCII)))return step;
        return -1;
    }
}
