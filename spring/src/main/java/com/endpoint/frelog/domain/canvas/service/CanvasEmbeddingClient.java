package com.endpoint.frelog.domain.canvas.service;

import com.fasterxml.jackson.databind.JsonNode;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.stereotype.Component;
import org.springframework.web.client.RestClient;

import javax.net.ssl.SSLContext;
import javax.net.ssl.SSLParameters;
import javax.net.ssl.TrustManagerFactory;
import java.net.URI;
import java.net.http.HttpClient;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.time.Duration;
import java.util.ArrayList;
import java.util.List;
import java.util.Map;

/** Query embeddings travel through Wall, never directly to the model worker. */
@Component
public class CanvasEmbeddingClient {
    public record Embedding(List<Double> vector, String revision) {}
    private final String url, ca, token;
    private final ObjectMapper mapper;
    private volatile RestClient client;
    public CanvasEmbeddingClient(@Value("${app.canvas-search.embedding-url:https://agora-nginx:8444/internal/search/embedding}") String url,
            @Value("${SERVICE_TLS_CA:}") String ca,
            @Value("${CPP_INTERNAL_API_TOKEN:}") String token, ObjectMapper mapper) {
        URI endpoint = URI.create(url);
        if (!"https".equals(endpoint.getScheme()) || endpoint.getHost() == null || endpoint.getUserInfo() != null
                || endpoint.getQuery() != null || endpoint.getFragment() != null) throw new IllegalArgumentException("Embedding URL must be HTTPS without credentials");
        this.url = url; this.ca = ca; this.token = token; this.mapper = mapper;
    }
    private synchronized RestClient client() throws Exception {
        if (client != null) return client;
        if (token.getBytes(java.nio.charset.StandardCharsets.UTF_8).length < 32) throw new IllegalStateException("Internal token is required");
        SSLParameters parameters = new SSLParameters(); parameters.setProtocols(new String[]{"TLSv1.3"});
        var builder = HttpClient.newBuilder().sslParameters(parameters).connectTimeout(Duration.ofMillis(700));
        if (!ca.isBlank()) {
            KeyStore store = KeyStore.getInstance(KeyStore.getDefaultType()); store.load(null, null);
            try (var input = Files.newInputStream(Path.of(ca))) {
                int i = 0;
                for (var certificate : CertificateFactory.getInstance("X.509").generateCertificates(input))
                    store.setCertificateEntry("ca-" + i++, certificate);
            }
            TrustManagerFactory trust = TrustManagerFactory.getInstance(TrustManagerFactory.getDefaultAlgorithm()); trust.init(store);
            SSLContext context = SSLContext.getInstance("TLS"); context.init(null, trust.getTrustManagers(), null);
            builder.sslContext(context);
        }
        var factory = new JdkClientHttpRequestFactory(builder.build()); factory.setReadTimeout(Duration.ofSeconds(2));
        client = RestClient.builder().requestFactory(factory).build();
        return client;
    }
    public Embedding embed(String query) throws Exception {
        String response = client().post().uri(url).header("X-Agora-Internal-Token", token)
                .contentType(org.springframework.http.MediaType.APPLICATION_JSON).body(Map.of("texts", List.of(query)))
                .retrieve().body(String.class);
        JsonNode root = mapper.readTree(response), values = root.path("vectors").path(0);
        String revision = root.path("model_revision").asText();
        if (!values.isArray() || values.size() != 384 || revision.isBlank()) throw new IllegalArgumentException("Invalid embedding response");
        List<Double> vector = new ArrayList<>(384); double norm = 0;
        for (JsonNode value : values) {
            double number = value.asDouble(Double.NaN);
            if (!value.isNumber() || !Double.isFinite(number)) throw new IllegalArgumentException("Invalid embedding vector");
            vector.add(number); norm += number * number;
        }
        if (norm < .99 || norm > 1.01) throw new IllegalArgumentException("Embedding must be normalized");
        return new Embedding(vector, revision);
    }
}
