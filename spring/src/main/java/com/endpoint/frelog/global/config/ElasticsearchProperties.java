package com.endpoint.frelog.global.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

/**
 * Elasticsearch 접속 IP, 포트, 인증 정보 및 인덱스 관리를 위한 설정 프로퍼티
 */
@Configuration
@ConfigurationProperties(prefix = "app.elasticsearch")
public class ElasticsearchProperties {

    private String searchIndex = "canvas-search";
    private boolean semanticSearchEnabled = true;
    private int searchLimit = 100;
    private int searchCandidates = 500;
    private double searchSimilarity = .80;
    public String getSearchIndex() { return searchIndex; }
    public void setSearchIndex(String value) { searchIndex = value; }
    public boolean isSemanticSearchEnabled() { return semanticSearchEnabled; }
    public void setSemanticSearchEnabled(boolean value) { semanticSearchEnabled = value; }
    public int getSearchLimit() { return searchLimit; }
    public void setSearchLimit(int value) { if (value < 1 || value > 200) throw new IllegalArgumentException("Search limit must be 1..200"); searchLimit = value; }
    public int getSearchCandidates() { return Math.max(searchCandidates, searchLimit); }
    public void setSearchCandidates(int value) { if (value < 1 || value > 2000) throw new IllegalArgumentException("Search candidates must be 1..2000"); searchCandidates = value; }
    public double getSearchSimilarity() { return searchSimilarity; }
    public void setSearchSimilarity(double value) { if (!Double.isFinite(value) || value < -1 || value > 1) throw new IllegalArgumentException("Invalid cosine threshold"); searchSimilarity = value; }
    private String host = "127.0.0.1";
    private int port = 9200;
    private String scheme = "https";
    private String caCertificate = "";
    private String index = "canvas";
    private String username = "agora_user";
    private String password;
    private String logIndex = "agora-logs";
    private String logUsername = "agora_log_writer";
    private String logPassword;
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

    public String getCaCertificate() {
        return caCertificate;
    }

    public void setCaCertificate(String caCertificate) {
        this.caCertificate = caCertificate;
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

    public String getLogIndex() {
        return logIndex;
    }

    public void setLogIndex(String logIndex) {
        this.logIndex = logIndex;
    }

    public String getLogUsername() {
        return logUsername;
    }

    public void setLogUsername(String logUsername) {
        this.logUsername = logUsername;
    }

    public String getLogPassword() {
        return logPassword;
    }

    public void setLogPassword(String logPassword) {
        this.logPassword = logPassword;
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
        if (!"https".equalsIgnoreCase(scheme)) {
            throw new IllegalStateException("ES_SCHEME must be https");
        }
        return String.format("%s://%s:%d", scheme, host, port);
    }
}
