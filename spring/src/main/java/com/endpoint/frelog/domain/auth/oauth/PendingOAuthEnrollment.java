package com.endpoint.frelog.domain.auth.oauth;
import java.time.Instant;
public record PendingOAuthEnrollment(SocialIdentity identity,String token,String origin,Instant expiresAt) {}
