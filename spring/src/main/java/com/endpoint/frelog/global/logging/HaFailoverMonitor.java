package com.endpoint.frelog.global.logging;

import com.endpoint.frelog.global.config.DatabaseProperties;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.ResultSet;
import java.sql.Statement;
import java.util.Map;
import java.util.Locale;

/** Observes the SQL AG listener and records loss, recovery, and primary changes. */
@Component
public class HaFailoverMonitor {
    private final DataSource dataSource;
    private final DatabaseProperties databaseProperties;
    private final ElasticsearchBulkLogService logService;

    public HaFailoverMonitor(DataSource dataSource, DatabaseProperties databaseProperties,
                             ElasticsearchBulkLogService logService) {
        this.dataSource = dataSource;
        this.databaseProperties = databaseProperties;
        this.logService = logService;
    }

    @Scheduled(fixedDelayString = "${app.ha.failover-monitor-interval-ms:10000}")
    public void observeSqlPrimary() {
        try (Connection connection = dataSource.getConnection()) {
            String product = connection.getMetaData().getDatabaseProductName();
            if (product == null || !product.toLowerCase(Locale.ROOT).contains("sql server")) return;

            String serverName;
            try (Statement statement = connection.createStatement()) {
                statement.setQueryTimeout(3);
                try (ResultSet result = statement.executeQuery("SELECT @@SERVERNAME")) {
                    if (!result.next()) throw new IllegalStateException("SQL Server did not return its instance name");
                    serverName = result.getString(1);
                }
            }

            Map<String, String> details = Map.of("listener", databaseProperties.getAddress());
            logService.reportAvailability("mssql-ag", true, details);
            logService.reportPrimaryChange("mssql-ag", serverName);
        } catch (Exception e) {
            logService.reportAvailability("mssql-ag", false,
                    Map.of("error_type", e.getClass().getSimpleName(),
                            "listener", databaseProperties.getAddress()));
        }
    }
}
