package com.endpoint.frelog.domain.loadbalancer.controller;

import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.DatabaseAddressResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.RedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.RegisterRedisRequest;
import com.endpoint.frelog.domain.loadbalancer.dto.RegisterServerRequest;
import com.endpoint.frelog.domain.loadbalancer.dto.ServerResponse;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.exception.GlobalExceptionHandler;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.http.MediaType;
import org.springframework.test.web.servlet.MockMvc;
import org.springframework.test.web.servlet.setup.MockMvcBuilders;

import static org.mockito.ArgumentMatchers.any;
import static org.mockito.BDDMockito.given;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.get;
import static org.springframework.test.web.servlet.request.MockMvcRequestBuilders.post;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.jsonPath;
import static org.springframework.test.web.servlet.result.MockMvcResultMatchers.status;

@ExtendWith(MockitoExtension.class)
class LoadBalancerControllerTest {

    private MockMvc mockMvc;
    private final ObjectMapper objectMapper = new ObjectMapper();

    @Mock
    private LoadBalancerService loadBalancerService;

    @InjectMocks
    private LoadBalancerController loadBalancerController;

    @BeforeEach
    void setUp() {
        mockMvc = MockMvcBuilders.standaloneSetup(loadBalancerController)
                .setControllerAdvice(new GlobalExceptionHandler())
                .build();
    }

    @Test
    @DisplayName("서버 할당 API 성공 시 200 OK 및 IP, Port 반환")
    void allocateServer_Success() throws Exception {
        given(loadBalancerService.allocateServer())
                .willReturn(AllocateServerResponse.of("127.0.0.1", "8000", "8002"));

        mockMvc.perform(post("/api/load-balancer/allocate/server"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.ip").value("127.0.0.1"))
                .andExpect(jsonPath("$.port").value("8000"))
                .andExpect(jsonPath("$.serverIp").value("127.0.0.1"))
                .andExpect(jsonPath("$.serverPort").value("8000"));
    }

    @Test
    @DisplayName("서버 할당 API 호출 시 등록된 서버가 없으면 404 및 '등록된 서버가 없습니다.' 에러 메시지 반환")
    void allocateServer_NoServerAvailable() throws Exception {
        given(loadBalancerService.allocateServer())
                .willThrow(new CustomException(ErrorCode.NO_SERVER_AVAILABLE, "등록된 서버가 없습니다."));

        mockMvc.perform(post("/api/load-balancer/allocate/server"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.code").value("LB_001"))
                .andExpect(jsonPath("$.message").value("등록된 서버가 없습니다."));
    }

    @Test
    @DisplayName("Redis 할당 API 성공 시 200 OK 및 IP, Port 반환")
    void allocateRedis_Success() throws Exception {
        given(loadBalancerService.allocateRedis())
                .willReturn(AllocateRedisResponse.of("127.0.0.1", "6379"));

        mockMvc.perform(post("/api/load-balancer/allocate/redis"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.ip").value("127.0.0.1"))
                .andExpect(jsonPath("$.port").value("6379"))
                .andExpect(jsonPath("$.redisIp").value("127.0.0.1"))
                .andExpect(jsonPath("$.redisPort").value("6379"));
    }

    @Test
    @DisplayName("Redis 할당 API 호출 시 등록된 Redis가 없으면 404 및 '등록된 Redis 서버가 없습니다.' 에러 메시지 반환")
    void allocateRedis_NoRedisAvailable() throws Exception {
        given(loadBalancerService.allocateRedis())
                .willThrow(new CustomException(ErrorCode.NO_REDIS_AVAILABLE, "등록된 Redis 서버가 없습니다."));

        mockMvc.perform(post("/api/load-balancer/allocate/redis"))
                .andExpect(status().isNotFound())
                .andExpect(jsonPath("$.code").value("LB_002"))
                .andExpect(jsonPath("$.message").value("등록된 Redis 서버가 없습니다."));
    }



    @Test
    @DisplayName("데이터베이스 주소 조회 API 성공 시 200 OK 및 DB 주소 정보 반환")
    void getDatabaseAddress_Success() throws Exception {
        DatabaseAddressResponse response =
                DatabaseAddressResponse.of("127.0.0.1", 1433, "agora_db", "127.0.0.1:1433");
        given(loadBalancerService.getDatabaseAddress()).willReturn(response);

        mockMvc.perform(get("/api/load-balancer/database"))
                .andExpect(status().isOk())
                .andExpect(jsonPath("$.ip").value("127.0.0.1"))
                .andExpect(jsonPath("$.port").value("1433"))
                .andExpect(jsonPath("$.dbHost").value("127.0.0.1"))
                .andExpect(jsonPath("$.dbPort").value(1433))
                .andExpect(jsonPath("$.dbName").value("agora_db"))
                .andExpect(jsonPath("$.address").value("127.0.0.1:1433"));
    }
}
