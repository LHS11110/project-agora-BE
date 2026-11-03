package com.endpoint.frelog.domain.auth.mfa;
import org.springframework.context.annotation.*;
import java.time.Clock;
@Configuration public class MfaConfiguration { @Bean public Clock mfaClock(){return Clock.systemUTC();} }
