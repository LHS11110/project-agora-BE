package com.endpoint.frelog.domain.loadbalancer.service;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.DatabaseAddressResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.RegisterServerRequest;
import com.endpoint.frelog.domain.loadbalancer.dto.ServerResponse;
import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import com.endpoint.frelog.global.config.DatabaseProperties;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;

import java.util.Collections;
import java.util.List;
import java.time.LocalDateTime;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.BDDMockito.given;
import static org.mockito.ArgumentMatchers.any;

@ExtendWith(MockitoExtension.class)
class LoadBalancerServiceTest {

    @Mock
    private ServerInfoRepository serverInfoRepository;

    @Mock
    private RedisInfoRepository redisInfoRepository;

    @Mock
    private CanvasInfoRepository canvasInfoRepository;

    @Mock
    private CppServerClient cppServerClient;

    @Mock
    private DatabaseProperties databaseProperties;

    @InjectMocks
    private LoadBalancerService loadBalancerService;

    @Test
    @DisplayName("등록된 활성 서버가 없을 때 예외 발생")
    void allocateServer_EmptyList_ThrowsException() {
        given(serverInfoRepository.findByIsActivatedTrueAndLastHeartbeatAtAfter(any(LocalDateTime.class))).willReturn(Collections.emptyList());

        assertThatThrownBy(() -> loadBalancerService.allocateServer())
                .isInstanceOf(CustomException.class)
                .hasMessage("활성화된 C++ 서버가 없습니다.")
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.NO_SERVER_AVAILABLE);
    }

    @Test
    @DisplayName("등록된 활성 Redis가 없을 때 예외 발생")
    void allocateRedis_EmptyList_ThrowsException() {
        given(redisInfoRepository.findByIsActivatedTrue()).willReturn(Collections.emptyList());

        assertThatThrownBy(() -> loadBalancerService.allocateRedis())
                .isInstanceOf(CustomException.class)
                .hasMessage("활성화된 Redis 서버가 없습니다.")
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.NO_REDIS_AVAILABLE);
    }

    @Test
    @DisplayName("서버가 1대만 활성화되어 있으면 해당 서버 IP와 Port를 바로 반환")
    void allocateServer_SingleServer_ReturnsDirectly() {
        ServerInfo single = new ServerInfo("127.0.0.1", "8000", "8002", "Main-Cpp");
        single.setIsActivated(true);
        single.setLastHeartbeatAt(LocalDateTime.now());
        given(serverInfoRepository.findByIsActivatedTrueAndLastHeartbeatAtAfter(any(LocalDateTime.class))).willReturn(List.of(single));
        given(cppServerClient.isHealthy("127.0.0.1", "8000")).willReturn(true);

        AllocateServerResponse response = loadBalancerService.allocateServer();

        assertThat(response.ip()).isEqualTo("127.0.0.1");
        assertThat(response.port()).isEqualTo("8000");
        assertThat(response.serverIp()).isEqualTo("127.0.0.1");
        assertThat(response.serverPort()).isEqualTo("8000");
    }

    @Test
    @DisplayName("Redis가 1대만 활성화되어 있으면 해당 Redis IP와 Port를 바로 반환")
    void allocateRedis_SingleRedis_ReturnsDirectly() {
        RedisInfo single = new RedisInfo("127.0.0.1", "6379", "Main-Redis");
        single.setIsActivated(true);
        given(redisInfoRepository.findByIsActivatedTrue()).willReturn(List.of(single));

        AllocateRedisResponse response = loadBalancerService.allocateRedis();

        assertThat(response.ip()).isEqualTo("127.0.0.1");
        assertThat(response.port()).isEqualTo("6379");
        assertThat(response.redisIp()).isEqualTo("127.0.0.1");
        assertThat(response.redisPort()).isEqualTo("6379");
    }

    @Test
    @DisplayName("서버가 2대일 때 Power of Two Choices 알고리즘으로 부하(캔버스 수)가 더 적은 서버 선택")
    void allocateServer_P2C_PicksLowerLoad() {
        RegisterServerRequest request = new RegisterServerRequest("127.0.0.1", "8000", "8002", "Server-1");
        ServerInfo s1 = new ServerInfo(request.serverIp(), request.serverPort(), request.wsPort(), request.serverName());
        s1.setIsActivated(true);
        ServerInfo s2 = new ServerInfo("127.0.0.1", "8001", "8003", "Server-2");
        s2.setIsActivated(true);
        s1.setLastHeartbeatAt(LocalDateTime.now());
        s2.setLastHeartbeatAt(LocalDateTime.now());
        given(serverInfoRepository.findByIsActivatedTrueAndLastHeartbeatAtAfter(any(LocalDateTime.class))).willReturn(List.of(s1, s2));
        given(cppServerClient.isHealthy("127.0.0.1", "8000")).willReturn(true);
        given(cppServerClient.isHealthy("127.0.0.1", "8001")).willReturn(true);

        // s1: 부하 10, s2: 부하 3
        given(cppServerClient.getCanvasCountFromServer("127.0.0.1", "8000")).willReturn(10);
        given(cppServerClient.getCanvasCountFromServer("127.0.0.1", "8001")).willReturn(3);

        AllocateServerResponse response = loadBalancerService.allocateServer();

        assertThat(response.port()).isEqualTo("8001");
    }

    @Test
    @DisplayName("Redis가 2대일 때 자바(Spring) 기반 Power of Two Choices 알고리즘으로 부하(캔버스 수)가 더 적은 Redis 선택")
    void allocateRedis_P2C_PicksLowerLoad_InJava() {
        RedisInfo r1 = new RedisInfo("127.0.0.1", "6379", "Redis-1");
        r1.setIsActivated(true);
        RedisInfo r2 = new RedisInfo("127.0.0.1", "6380", "Redis-2");
        r2.setIsActivated(true);
        given(redisInfoRepository.findByIsActivatedTrue()).willReturn(List.of(r1, r2));

        // 자바 내 CanvasInfoRepository 카운트 기준: r1은 12개, r2는 4개
        given(canvasInfoRepository.countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue("127.0.0.1", "6379")).willReturn(12L);
        given(canvasInfoRepository.countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue("127.0.0.1", "6380")).willReturn(4L);

        AllocateRedisResponse response = loadBalancerService.allocateRedis();

        assertThat(response.port()).isEqualTo("6380");
        assertThat(response.redisPort()).isEqualTo("6380");
    }

    @Test
    @DisplayName("데이터베이스 주소 조회 시 설정된 Host, Port, Name, Address 반환")
    void getDatabaseAddress_ReturnsConfiguredProperties() {
        given(databaseProperties.getHost()).willReturn("192.168.1.50");
        given(databaseProperties.getPort()).willReturn(1433);
        given(databaseProperties.getName()).willReturn("agora_db");
        given(databaseProperties.getAddress()).willReturn("192.168.1.50:1433");

        DatabaseAddressResponse response = loadBalancerService.getDatabaseAddress();

        assertThat(response.ip()).isEqualTo("192.168.1.50");
        assertThat(response.port()).isEqualTo("1433");
        assertThat(response.dbHost()).isEqualTo("192.168.1.50");
        assertThat(response.dbPort()).isEqualTo(1433);
        assertThat(response.dbName()).isEqualTo("agora_db");
        assertThat(response.address()).isEqualTo("192.168.1.50:1433");
    }
}
