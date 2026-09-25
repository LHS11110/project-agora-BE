package com.endpoint.frelog.global.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.http.client.JdkClientHttpRequestFactory;
import org.springframework.web.client.RestClient;

import java.net.http.HttpClient;
import java.io.InputStream;
import java.nio.file.Files;
import java.nio.file.Path;
import java.security.KeyStore;
import java.security.cert.CertificateFactory;
import java.security.cert.X509Certificate;
import javax.net.ssl.SSLContext;
import javax.net.ssl.TrustManagerFactory;
import java.nio.charset.StandardCharsets;
import java.time.Duration;
import java.util.Base64;

@Configuration
public class ElasticsearchConfig {

    private final ElasticsearchProperties properties;

    public ElasticsearchConfig(ElasticsearchProperties properties) {
        this.properties = properties;
    }

    @Bean
    @ConditionalOnMissingBean
    public ObjectMapper objectMapper() {
        return new ObjectMapper();
    }

    @Bean(name = "elasticsearchRestClient")
    public RestClient elasticsearchRestClient() throws Exception {
        return createRestClient(properties.getUsername(), properties.getPassword());
    }

    @Bean(name = "elasticsearchLogRestClient")
    public RestClient elasticsearchLogRestClient() throws Exception {
        return createRestClient(properties.getLogUsername(), properties.getLogPassword());
    }

    private RestClient createRestClient(String username, String password) throws Exception {
        HttpClient.Builder httpClient = HttpClient.newBuilder()
                .connectTimeout(Duration.ofSeconds(2));
        if ("https".equalsIgnoreCase(properties.getScheme())
                && properties.getCaCertificate() != null
                && !properties.getCaCertificate().isBlank()) {
            CertificateFactory certificates = CertificateFactory.getInstance("X.509");
            X509Certificate ca;
            try (InputStream input = Files.newInputStream(Path.of(properties.getCaCertificate()))) {
                ca = (X509Certificate) certificates.generateCertificate(input);
            }
            KeyStore trustStore = KeyStore.getInstance(KeyStore.getDefaultType());
            trustStore.load(null);
            trustStore.setCertificateEntry("elasticsearch-ca", ca);
            TrustManagerFactory trustManagers = TrustManagerFactory.getInstance(
                    TrustManagerFactory.getDefaultAlgorithm());
            trustManagers.init(trustStore);
            SSLContext sslContext = SSLContext.getInstance("TLS");
            sslContext.init(null, trustManagers.getTrustManagers(), null);
            httpClient.sslContext(sslContext);
        }
        JdkClientHttpRequestFactory requestFactory = new JdkClientHttpRequestFactory(httpClient.build());
        requestFactory.setReadTimeout(Duration.ofSeconds(5));
        RestClient.Builder builder = RestClient.builder()
                .baseUrl(properties.getBaseUrl())
                .defaultHeader(HttpHeaders.CONTENT_TYPE, MediaType.APPLICATION_JSON_VALUE)
                .defaultHeader(HttpHeaders.ACCEPT, MediaType.APPLICATION_JSON_VALUE);
        builder.requestFactory(requestFactory);

        if (username != null && !username.isBlank() && password != null && !password.isBlank()) {
            String credentials = username + ":" + password;
            String authHeader = "Basic " + Base64.getEncoder().encodeToString(credentials.getBytes(StandardCharsets.UTF_8));
            builder.defaultHeader(HttpHeaders.AUTHORIZATION, authHeader);
        }

        return builder.build();
    }
}
