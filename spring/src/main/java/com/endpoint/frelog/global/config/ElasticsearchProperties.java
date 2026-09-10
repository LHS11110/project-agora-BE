package com.endpoint.frelog.global.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

/**
 * Elasticsearch 접속 IP, 포트, 인증 정보 및 인덱스 관리를 위한 설정 프로퍼티
 */
@Configuration
@ConfigurationProperties(prefix = "app.elasticsearch")
public class ElasticsearchProperties {

    private String host = "127.0.0.1";
    private int port = 9200;
    private String scheme = "http";
    private String index = "canvas";
    private String username = "agora_user";
    private String password = "AgoraUserSecret@Passw0rd!2026";
    private boolean autoCreate = true;
    private boolean failOnError = false;

    public String getHost() {
        return host;
    }

    public void setHost(String host) {
        this.host = host;
    }

    public int getPort() {
        return port;
    }

    public void setPort(int port) {
        this.port = port;
    }

    public String getScheme() {
        return scheme;
    }

    public void setScheme(String scheme) {
        this.scheme = scheme;
    }

    public String getIndex() {
        return index;
    }

    public void setIndex(String index) {
        this.index = index;
    }

    public String getUsername() {
        return username;
    }

    public void setUsername(String username) {
        this.username = username;
    }

    public String getPassword() {
        return password;
    }

    public void setPassword(String password) {
        this.password = password;
    }

    public boolean isAutoCreate() {
        return autoCreate;
    }

    public void setAutoCreate(boolean autoCreate) {
        this.autoCreate = autoCreate;
    }

    public boolean isFailOnError() {
        return failOnError;
    }

    public void setFailOnError(boolean failOnError) {
        this.failOnError = failOnError;
    }

    public String getBaseUrl() {
        return String.format("%s://%s:%d", scheme, host, port);
    }
}
