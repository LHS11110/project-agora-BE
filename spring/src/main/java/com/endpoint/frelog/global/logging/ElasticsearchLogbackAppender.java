package com.endpoint.frelog.global.logging;

import ch.qos.logback.classic.Logger;
import ch.qos.logback.classic.LoggerContext;
import ch.qos.logback.classic.spi.ILoggingEvent;
import ch.qos.logback.classic.spi.ThrowableProxyUtil;
import ch.qos.logback.core.AppenderBase;
import jakarta.annotation.PostConstruct;
import jakarta.annotation.PreDestroy;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Component;

import java.util.LinkedHashMap;
import java.util.Map;

/** Sends Spring Boot's normal SLF4J/Logback output to the asynchronous bulk log sink. */
@Component
public class ElasticsearchLogbackAppender {
    private final ElasticsearchBulkLogService logService;
    private Logger rootLogger;
    private AppenderBase<ILoggingEvent> appender;

    public ElasticsearchLogbackAppender(ElasticsearchBulkLogService logService) {
        this.logService = logService;
    }

    @PostConstruct
    public void attach() {
        Object factory = LoggerFactory.getILoggerFactory();
        if (!(factory instanceof LoggerContext context)) return;

        appender = new AppenderBase<>() {
            @Override
            protected void append(ILoggingEvent event) {
                String loggerName = event.getLoggerName();
                if (ElasticsearchBulkLogService.class.getName().equals(loggerName)
                        || ElasticsearchLogbackAppender.class.getName().equals(loggerName)) return;

                Map<String, Object> details = new LinkedHashMap<>();
                details.put("logger", loggerName);
                details.put("thread", event.getThreadName());
                Map<String, String> mdc = event.getMDCPropertyMap();
                if (mdc != null && !mdc.isEmpty()) details.put("mdc", mdc);
                if (event.getThrowableProxy() != null) {
                    details.put("exception", ThrowableProxyUtil.asString(event.getThrowableProxy()));
                }
                logService.record("spring", "application_log", event.getLevel().toString(),
                        event.getFormattedMessage(), details);
            }
        };
        appender.setName("ELASTICSEARCH_BULK_LOG_APPENDER");
        appender.setContext(context);
        appender.start();
        rootLogger = context.getLogger(Logger.ROOT_LOGGER_NAME);
        rootLogger.addAppender(appender);
    }

    @PreDestroy
    public void detach() {
        if (rootLogger != null && appender != null) {
            rootLogger.detachAppender(appender);
            appender.stop();
        }
    }
}
