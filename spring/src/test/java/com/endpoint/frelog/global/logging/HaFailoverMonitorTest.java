package com.endpoint.frelog.global.logging;

import com.endpoint.frelog.global.config.DatabaseProperties;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

import javax.sql.DataSource;
import java.sql.Connection;
import java.sql.DatabaseMetaData;
import java.sql.ResultSet;
import java.sql.SQLException;
import java.sql.Statement;
import java.util.Map;
import java.util.concurrent.atomic.AtomicBoolean;
import java.util.concurrent.atomic.AtomicReference;

import static org.mockito.ArgumentMatchers.anyMap;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.mock;
import static org.mockito.Mockito.times;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.when;

@ExtendWith(MockitoExtension.class)
class HaFailoverMonitorTest {

    @Mock
    private DataSource dataSource;

    @Mock
    private DatabaseProperties databaseProperties;

    @Mock
    private ElasticsearchBulkLogService logService;

    @Test
    void observesSqlListenerOutageRecoveryAndPrimaryChange() throws Exception {
        AtomicBoolean listenerAvailable = new AtomicBoolean(false);
        AtomicReference<String> activePrimary = new AtomicReference<>("sql-primary-a");
        when(databaseProperties.getAddress()).thenReturn("ag-listener.test:1433");
        when(dataSource.getConnection()).thenAnswer(invocation -> {
            if (!listenerAvailable.get()) throw new SQLException("simulated AG listener outage");
            return sqlServerConnection(activePrimary.get());
        });

        HaFailoverMonitor monitor = new HaFailoverMonitor(dataSource, databaseProperties, logService);

        // Inject a connection failure, then restore the listener and change its primary.
        monitor.observeSqlPrimary();
        listenerAvailable.set(true);
        monitor.observeSqlPrimary();
        activePrimary.set("sql-primary-b");
        monitor.observeSqlPrimary();

        verify(logService).reportAvailability(eq("mssql-ag"), eq(false), anyMap());
        verify(logService, times(2)).reportAvailability(eq("mssql-ag"), eq(true), anyMap());
        verify(logService).reportPrimaryChange("mssql-ag", "sql-primary-a");
        verify(logService).reportPrimaryChange("mssql-ag", "sql-primary-b");
    }

    private static Connection sqlServerConnection(String primary) throws SQLException {
        Connection connection = mock(Connection.class);
        DatabaseMetaData metadata = mock(DatabaseMetaData.class);
        Statement statement = mock(Statement.class);
        ResultSet resultSet = mock(ResultSet.class);
        when(connection.getMetaData()).thenReturn(metadata);
        when(metadata.getDatabaseProductName()).thenReturn("Microsoft SQL Server");
        when(connection.createStatement()).thenReturn(statement);
        when(statement.executeQuery("SELECT @@SERVERNAME")).thenReturn(resultSet);
        when(resultSet.next()).thenReturn(true);
        when(resultSet.getString(1)).thenReturn(primary);
        return connection;
    }
}
