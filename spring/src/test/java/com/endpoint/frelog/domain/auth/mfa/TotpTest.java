package com.endpoint.frelog.domain.auth.mfa;
import org.junit.jupiter.api.Test;
import static org.assertj.core.api.Assertions.*;
import java.nio.charset.StandardCharsets;
class TotpTest {
 @Test void rfc6238Sha1VectorsAndBase32RoundTrip(){String seed=Totp.base32("12345678901234567890".getBytes(StandardCharsets.US_ASCII));assertThat(new String(Totp.decode(seed),StandardCharsets.US_ASCII)).isEqualTo("12345678901234567890");long[] times={59,1111111109L,1111111111L,1234567890L,2000000000L,20000000000L};String[] expected={"287082","081804","050471","005924","279037","353130"};for(int i=0;i<times.length;i++)assertThat(Totp.code(seed,times[i]/30)).isEqualTo(expected[i]);}
 @Test void boundedWindowRejectsReplayAndMalformedInput(){String seed=Totp.base32(new byte[20]);assertThat(Totp.match(seed,Totp.code(seed,100),100,99)).isEqualTo(100);assertThat(Totp.match(seed,Totp.code(seed,100),100,100)).isNegative();assertThat(Totp.match(seed,Totp.code(seed,97),100,-1)).isNegative();assertThat(Totp.match(seed,"abcdef",100,-1)).isNegative();}
}
