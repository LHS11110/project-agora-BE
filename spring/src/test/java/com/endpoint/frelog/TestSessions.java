package com.endpoint.frelog;

import com.endpoint.frelog.domain.auth.session.AuthSessionService;
import com.endpoint.frelog.domain.user.entity.User;
import org.springframework.mock.web.MockHttpServletRequest;

/** Issues a real session-bound access token for integration tests. */
public final class TestSessions {
    private TestSessions(){}
    public static String token(AuthSessionService sessions, User user, String ip) {
        var request=new MockHttpServletRequest();request.setRemoteAddr(ip);
        return sessions.issue(user,request,null);
    }
}
