package com.endpoint.frelog;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;
import org.springframework.cache.annotation.EnableCaching;
import org.springframework.data.elasticsearch.repository.config.EnableElasticsearchRepositories;
import org.springframework.data.jpa.repository.config.EnableJpaRepositories;
import org.springframework.scheduling.annotation.EnableScheduling;

@SpringBootApplication
@EnableCaching
@EnableScheduling
@EnableJpaRepositories(basePackages = "com.endpoint.frelog.repository.jpa")
@EnableElasticsearchRepositories(basePackages = "com.endpoint.frelog.repository.elasticsearch")
public class FrelogApplication {

	public static void main(String[] args) {
		SpringApplication.run(FrelogApplication.class, args);
	}

}
