package com.endpoint.frelog.global.config;

import org.apache.catalina.Context;
import org.springframework.boot.tomcat.servlet.TomcatServletWebServerFactory;
import org.springframework.boot.web.server.WebServerFactoryCustomizer;
import org.springframework.context.annotation.Bean;
import org.springframework.context.annotation.Configuration;

import java.io.File;

@Configuration
public class TomcatJspConfig {

    @Bean
    public WebServerFactoryCustomizer<TomcatServletWebServerFactory> tomcatCustomizer() {
        return factory -> {
            File docBase = new File("spring/src/main/webapp");
            if (!docBase.exists()) {
                docBase = new File("src/main/webapp");
            }
            if (docBase.exists()) {
                factory.setDocumentRoot(docBase);
            }
        };
    }
}
