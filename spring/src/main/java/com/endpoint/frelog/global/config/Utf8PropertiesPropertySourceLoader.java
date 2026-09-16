package com.endpoint.frelog.global.config;

import org.springframework.boot.env.PropertiesPropertySourceLoader;
import org.springframework.core.Ordered;
import org.springframework.core.env.PropertySource;
import org.springframework.core.io.Resource;

import java.io.IOException;
import java.nio.charset.Charset;
import java.nio.charset.StandardCharsets;
import java.util.List;

/**
 * Ensures .properties files are loaded using UTF-8 encoding by default instead of ISO-8859-1.
 */
public class Utf8PropertiesPropertySourceLoader extends PropertiesPropertySourceLoader implements Ordered {

    @Override
    public int getOrder() {
        return Ordered.HIGHEST_PRECEDENCE;
    }

    @Override
    public List<PropertySource<?>> load(String name, Resource resource) throws IOException {
        return super.load(name, resource, StandardCharsets.UTF_8);
    }

    @Override
    public List<PropertySource<?>> load(String name, Resource resource, Charset charset) throws IOException {
        return super.load(name, resource, charset != null ? charset : StandardCharsets.UTF_8);
    }
}
