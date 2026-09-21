package com.endpoint.frelog.global.config;

import org.springframework.boot.context.properties.ConfigurationProperties;
import org.springframework.context.annotation.Configuration;

/**
 * DB 접속 IP 및 포트를 별도로 관리하기 위한 설정 프로퍼티
 */
@Configuration
@ConfigurationProperties(prefix = "app.db")
public class DatabaseProperties {

    private String address;
    private String host = "127.0.0.1";
    private int port = 1433;
    private String name = "agora_db";
    private String username = "agora_user";
    private String password;

    public String getAddress() {
        if (address != null && !address.isBlank()) {
            return address;
        }
        return host + ":" + port;
    }

    public void setAddress(String address) {
        this.address = address;
        if (address != null && !address.isBlank()) {
            String trimmed = address.trim();
            if (trimmed.contains(":")) {
                String[] parts = trimmed.split(":", 2);
                this.host = parts[0].trim();
                try {
                    this.port = Integer.parseInt(parts[1].trim());
                } catch (NumberFormatException ignored) {
                }
            } else {
                this.host = trimmed;
            }
        }
    }

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
