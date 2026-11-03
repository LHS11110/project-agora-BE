package com.endpoint.frelog.domain.auth.oauth;
/** The provider authenticated the account, but did not establish a verified email for signup. */
public class OAuthEmailRequired extends RuntimeException {}
