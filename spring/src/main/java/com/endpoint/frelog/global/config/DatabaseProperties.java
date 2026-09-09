package com.endpoint.frelog.global.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

/**
 * DB 접속 IP 및 포트를 별도로 관리하기 위한 설정 프로퍼티
 */
@Configuration
@ConfigurationProperties(prefix = "app.db")
public class DatabaseProperties {

    private String host = "127.0.0.1";
    private int port = 1433;
    private String name = "agora_db";
    private String username = "agora_user";
    private String password = "AgoraUserSecret@Passw0rd!2026";

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

    public String getName() {
        return name;
    }

    public void setName(String name) {
        this.name = name;
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
}
