package com.endpoint.frelog.domain.canvas.client;

import org.junit.jupiter.api.Test;
import org.springframework.test.web.client.MockRestServiceServer;
import org.springframework.web.client.RestClient;
import org.springframework.http.MediaType;
import org.springframework.http.HttpStatus;
import static org.assertj.core.api.Assertions.*;
import static org.springframework.test.web.client.match.MockRestRequestMatchers.*;
import static org.springframework.test.web.client.response.MockRestResponseCreators.*;

class CppServerClientTest {
    @Test void routesOnlyByServerIdAndPreservesAuthentication() {
        var builder = RestClient.builder();
        var mock = MockRestServiceServer.bindTo(builder).build();
        var client = new CppServerClient(builder.build(), "https://wall.example:8444/", "internal-token");
        mock.expect(requestTo("https://wall.example:8444/internal/cpp/servers/42/api/canvas/count"))
                .andExpect(header("X-Agora-Internal-Token", "internal-token"))
                .andExpect(header("X-Request-ID", org.hamcrest.Matchers.not(org.hamcrest.Matchers.emptyString())))
                .andRespond(withSuccess("{\"count\":7}", MediaType.APPLICATION_JSON));
        assertThat(client.getCanvasCountFromServer(42)).isEqualTo(7);
        mock.verify();
    }
    @Test void unknownRouteDoesNotFallBackToDirectCppConnection() {
        var builder = RestClient.builder();
        var mock = MockRestServiceServer.bindTo(builder).build();
        var client = new CppServerClient(builder.build(), "https://wall.example:8444", "token");
        mock.expect(requestTo("https://wall.example:8444/internal/cpp/servers/99/api/canvas/count"))
                .andRespond(withStatus(HttpStatus.NOT_FOUND));
        assertThat(client.getCanvasCountFromServer(99)).isEqualTo(Integer.MAX_VALUE);
        assertThat(client.isHealthy(null)).isFalse();
        assertThat(client.isHealthy(0)).isFalse();
        mock.verify();
    }
    @Test void rejectsPlaintextAndOriginsContainingPathsOrCredentials() {
        for (String origin : new String[]{"http://wall.example", "https://wall.example/api", "https://user@wall.example", "https://wall.example?host=cpp"}) {
            assertThatThrownBy(() -> new CppServerClient(RestClient.create(), origin, "token"))
                    .isInstanceOf(IllegalArgumentException.class);
        }
    }
}
