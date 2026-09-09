package com.endpoint.frelog.global.config;

import com.zaxxer.hikari.HikariDataSource;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;
import org.springframework.context.annotation.Primary;

import javax.sql.DataSource;
import java.net.InetSocketAddress;
import java.net.Socket;

@Configuration
public class DataSourceConfig {

    private static final Logger log = LoggerFactory.getLogger(DataSourceConfig.class);

    private final DatabaseProperties dbProperties;

    @Value("${app.db.auto-fallback:true}")
    private boolean autoFallback;

    public DataSourceConfig(DatabaseProperties dbProperties) {
        this.dbProperties = dbProperties;
    }

    @Bean
    @Primary
    public DataSource dataSource() {
        String host = dbProperties.getHost();
        int port = dbProperties.getPort();

        boolean reachable = isHostPortReachable(host, port, 1200);

        if (reachable) {
            log.info("MSSQL 데이터베이스({}:{}) 연결을 설정합니다. DB: {}", host, port, dbProperties.getName());
            HikariDataSource ds = new HikariDataSource();
            ds.setDriverClassName("com.microsoft.sqlserver.jdbc.SQLServerDriver");
            ds.setJdbcUrl(String.format("jdbc:sqlserver://%s:%d;databaseName=%s;encrypt=false;trustServerCertificate=true",
                    host, port, dbProperties.getName()));
            ds.setUsername(dbProperties.getUsername());
            ds.setPassword(dbProperties.getPassword());
            return ds;
        }

        if (autoFallback) {
            log.warn("===============================================================================");
            log.warn("MSSQL 서버({}:{})에 연결할 수 없습니다. 로컬 테스트 및 웹 실험을 위해 H2 인메모리 DB로 자동 전환합니다.", host, port);
            log.warn("실제 MSSQL 서버가 실행되면 별도 설정 변경 없이 해당 호스트({}:{})로 연결됩니다.", host, port);
            log.warn("===============================================================================");
            HikariDataSource h2Ds = new HikariDataSource();
            h2Ds.setDriverClassName("org.h2.Driver");
            h2Ds.setJdbcUrl("jdbc:h2:mem:agora_db;MODE=MSSQLServer;DB_CLOSE_DELAY=-1;DB_CLOSE_ON_EXIT=FALSE");
            h2Ds.setUsername("sa");
            h2Ds.setPassword("");
            return h2Ds;
        }

        throw new IllegalStateException(String.format("MSSQL 서버(%s:%d)에 연결할 수 없습니다.", host, port));
    }

    private boolean isHostPortReachable(String host, int port, int timeoutMs) {
        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(host, port), timeoutMs);
            return true;
        } catch (Exception e) {
            return false;
        }
    }
}
