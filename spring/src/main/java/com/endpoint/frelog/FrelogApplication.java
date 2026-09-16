package com.endpoint.frelog;

import org.springframework.boot.SpringApplication;
import org.springframework.boot.autoconfigure.SpringBootApplication;

import org.springframework.scheduling.annotation.EnableAsync;

@SpringBootApplication
@EnableAsync
public class FrelogApplication {

	public static void main(String[] args) {
		SpringApplication.run(FrelogApplication.class, args);
	}

}
