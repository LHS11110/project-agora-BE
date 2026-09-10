package com.endpoint.frelog.global.config;

import com.fasterxml.jackson.databind.ObjectMapper;
import org.springframework.boot.autoconfigure.condition.ConditionalOnMissingBean;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.http.HttpHeaders;
import org.springframework.http.MediaType;
import org.springframework.web.client.RestClient;

import java.nio.charset.StandardCharsets;
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
    public RestClient elasticsearchRestClient() {
        String authHeader = "";
        if (properties.getUsername() != null && properties.getPassword() != null) {
            String credentials = properties.getUsername() + ":" + properties.getPassword();
            authHeader = "Basic " + Base64.getEncoder().encodeToString(credentials.getBytes(StandardCharsets.UTF_8));
        }

        RestClient.Builder builder = RestClient.builder()
                .baseUrl(properties.getBaseUrl())
                .defaultHeader(HttpHeaders.CONTENT_TYPE, MediaType.APPLICATION_JSON_VALUE)
                .defaultHeader(HttpHeaders.ACCEPT, MediaType.APPLICATION_JSON_VALUE);

        if (!authHeader.isEmpty()) {
            builder.defaultHeader(HttpHeaders.AUTHORIZATION, authHeader);
        }

        return builder.build();
    }
}
