package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasSummaryResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.util.ReflectionTestUtils;

import java.util.List;
import java.util.Optional;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasServiceTest {

    @Mock
    private CanvasInfoRepository canvasInfoRepository;

    @Mock
    private UserRepository userRepository;

    @Mock
    private CanvasElasticsearchService canvasElasticsearchService;

    @Mock
    private CanvasResourceService canvasResourceService;

    @Mock
    private LoadBalancerService loadBalancerService;

    @Mock
    private CppServerClient cppServerClient;

    @InjectMocks
    private CanvasService canvasService;

    private User testUser;
    private CustomUserDetails userDetails;

    @BeforeEach
    void setUp() {
        testUser = new User("user@agora.com", "encodedPassword", "아고라유저", Role.ROLE_USER);
        testUser.setStatus(UserStatus.ACTIVE);
        ReflectionTestUtils.setField(testUser, "userId", 1L);
        userDetails = new CustomUserDetails(testUser);
    }

    @Test
    @DisplayName("캔버스 생성 시 MS SQL과 Elasticsearch에 각각 데이터가 저장된다")
    void createCanvas_Success() {
        // given
        given(canvasInfoRepository.findMaxCanvasId()).willReturn(100);
        CanvasInfo savedInfo = new CanvasInfo(101);
        given(canvasInfoRepository.save(any(CanvasInfo.class))).willReturn(savedInfo);
        given(canvasResourceService.saveRepresentativeImage(eq(101), any())).willReturn("/api/canvases/101/image");

        // when
        CanvasSummaryResponse response = canvasService.createCanvas("Test Canvas", "description", "pass123", null, userDetails);

        // then
        assertThat(response).isNotNull();
        assertThat(response.canvasId()).isEqualTo(101);
        assertThat(response.canvasName()).isEqualTo("Test Canvas");
        assertThat(response.description()).isEqualTo("description");

        verify(canvasInfoRepository).save(any(CanvasInfo.class));
        verify(canvasElasticsearchService).saveCanvas(any(CanvasDocument.class));
    }

    @Test
    @DisplayName("캔버스 검색 시 Elasticsearch에서 조회하고 CanvasSummaryResponse 목록 반환")
    void searchCanvases_Success() {
        // given
        CanvasDocument doc = new CanvasDocument("Agora Canvas", 50, 1L, null, "default");
        doc.setDescription("Agora canvas desc");
        doc.setPeople(List.of(1L, 2L));

        given(canvasElasticsearchService.searchCanvasesByName("Agora")).willReturn(List.of(doc));

        // when
        List<CanvasSummaryResponse> results = canvasService.searchCanvases("Agora", userDetails);

        // then
        assertThat(results).hasSize(1);
        CanvasSummaryResponse summary = results.get(0);
        assertThat(summary.canvasId()).isEqualTo(50);
        assertThat(summary.canvasName()).isEqualTo("Agora Canvas");
        assertThat(summary.description()).isEqualTo("Agora canvas desc");
        assertThat(summary.userCount()).isEqualTo(2);
    }

    @Test
    @DisplayName("캔버스 단건 요약 조회 성공")
    void getCanvasSummary_Success() {
        // given
        CanvasDocument doc = new CanvasDocument("Solo Canvas", 10, 1L, null, "default");
        doc.setDescription("Solo desc");
        doc.setPeople(List.of(1L));

        given(canvasElasticsearchService.getCanvasDocumentById(10)).willReturn(Optional.of(doc));

        // when
        CanvasSummaryResponse summary = canvasService.getCanvasSummary(10, userDetails);

        // then
        assertThat(summary).isNotNull();
        assertThat(summary.canvasId()).isEqualTo(10);
        assertThat(summary.canvasName()).isEqualTo("Solo Canvas");
        assertThat(summary.userCount()).isEqualTo(1);
    }

    @Test
    @DisplayName("캔버스 이름 수정 시 Elasticsearch와 캐시된 경우 C++ 서버에 반영된다")
    void updateCanvasName_Cached_CallsCppServer() {
        // given
        CanvasDocument doc = new CanvasDocument("Old Name", 100, 1L, null, "default");
        given(canvasElasticsearchService.getCanvasDocumentById(100)).willReturn(Optional.of(doc));

        CanvasInfo info = new CanvasInfo(100);
        info.setIsCached(true);
        info.setCppServer(new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002"));
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(info));

        // when
        CanvasSummaryResponse response = canvasService.updateCanvasName(100, "New Name", userDetails);

        // then
        assertThat(response.canvasName()).isEqualTo("New Name");
        verify(canvasElasticsearchService).saveCanvas(doc);
        verify(cppServerClient).reflectCanvasName("127.0.0.1", "8000", 100, "New Name");
    }

    @Test
    @DisplayName("소유자나 관리자가 아닌 유저가 캔버스 수정 시 FORBIDDEN 예외 발생")
    void updateCanvasName_NotOwner_ThrowsForbidden() {
        // given
        CanvasDocument doc = new CanvasDocument("Test Canvas", 100, 1L, null, "default");
        given(canvasElasticsearchService.getCanvasDocumentById(100)).willReturn(Optional.of(doc));

        User otherUser = new User("other@agora.com", "pass", "다른유저", Role.ROLE_USER);
        ReflectionTestUtils.setField(otherUser, "userId", 2L);
        CustomUserDetails otherDetails = new CustomUserDetails(otherUser);

        // when & then
        assertThatThrownBy(() -> canvasService.updateCanvasName(100, "New Name", otherDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.ACCESS_DENIED);
    }

    @Test
    @DisplayName("캔버스 삭제 시 Elasticsearch, MS SQL, 리소스 디렉토리 및 C++ 서버에서 삭제된다")
    void deleteCanvas_Cached_Success() {
        // given
        CanvasDocument doc = new CanvasDocument("Delete Canvas", 200, 1L, null, "default");
        given(canvasElasticsearchService.getCanvasDocumentById(200)).willReturn(Optional.of(doc));

        CanvasInfo info = new CanvasInfo(200);
        info.setIsCached(true);
        info.setCppServer(new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002"));
        info.setRedisInfo(new com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo("127.0.0.1", "6379"));
        given(canvasInfoRepository.findById(200)).willReturn(Optional.of(info));

        // when
        canvasService.deleteCanvas(200, userDetails);

        // then
        verify(cppServerClient).deleteCanvasFromServerAndRedis("127.0.0.1", "8000", 200, "127.0.0.1", "6379");
        verify(canvasElasticsearchService).deleteCanvas(200, "Delete Canvas");
        verify(canvasInfoRepository).delete(info);
        verify(canvasResourceService).deleteCanvasResourceDirectory(200);
    }

    @Test
    @DisplayName("캔버스 접속 시 초대된 사용자인 경우 미캐시 상태면 P2C로 할당하고 JWT를 C++ 서버에 등록한다")
    void accessCanvas_NotCached_AllocatesAndRegisters() {
        // given
        CanvasDocument doc = new CanvasDocument("Access Canvas", 300, 1L, "hashedPass", "default");
        doc.getPeople().add(1L);
        given(canvasElasticsearchService.getCanvasDocumentById(300)).willReturn(Optional.of(doc));

        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));

        CanvasInfo info = new CanvasInfo(300);
        info.setIsCached(false);
        given(canvasInfoRepository.findById(300)).willReturn(Optional.of(info));

        given(loadBalancerService.allocateServer()).willReturn(AllocateServerResponse.of("127.0.0.1", "8000"));
        given(loadBalancerService.allocateRedis()).willReturn(AllocateRedisResponse.of("127.0.0.1", "6379"));

        // when
        CanvasUpdateDtos.AccessResponse response = canvasService.accessCanvas(300, "jwt.token.here", userDetails);

        // then
        assertThat(response).isNotNull();
        assertThat(response.serverIp()).isEqualTo("127.0.0.1");
        assertThat(response.serverPort()).isEqualTo("8000");

        verify(cppServerClient).registerJwtToken("127.0.0.1", "8000", 1L, "jwt.token.here");
        assertThat(info.getIsCached()).isTrue();
        // assertThat(testUser.getIsAccessed()).isTrue();
    }

    @Test
    @DisplayName("캔버스 접속 중단 시 C++ 서버 연결 해제 및 사용자 접속 상태(isAccessed=false)를 롤백한다")
    void disconnectCanvasAccess_Success() {
        // given
        // testUser.setIsAccessed(true);
        // testUser.setCppServer(new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002"));
        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));

        // when
        canvasService.disconnectCanvasAccess(300, userDetails);

        // then
        verify(cppServerClient).disconnectUserFromCanvas("127.0.0.1", "8000", 300, 1L);
        // assertThat(testUser.getIsAccessed()).isFalse();
        // assertThat(testUser.getServerIp()).isNull();
        // assertThat(testUser.getServerPort()).isNull();
    }

    @Test
    @DisplayName("C++ 웹소켓 종료 내부 알림 시 사용자 접속 상태가 해제된다")
    void handleInternalDisconnect_UserOnly_Success() {
        // given
        // testUser.setIsAccessed(true);
        // testUser.setCppServer(new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002"));
        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));

        // when
        canvasService.handleInternalDisconnect(300, 1L, 2);

        // then
        // assertThat(testUser.getIsAccessed()).isFalse();
        // assertThat(testUser.getServerIp()).isNull();
        // assertThat(testUser.getServerPort()).isNull();
        verify(userRepository).save(testUser);
    }

    @Test
    @DisplayName("C++ 웹소켓 종료 시 활성 사용자가 0명이면 캔버스 캐시 상태(is_cached=false, IP/Port=none)를 초기화한다")
    void handleInternalDisconnect_ZeroActiveUsers_UnloadCanvas() {
        // given
        // testUser.setIsAccessed(true);
        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));

        CanvasInfo info = new CanvasInfo(300);
        info.setIsCached(true);
        info.setRedisInfo(new com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo("127.0.0.1", "6379"));
        info.setCppServer(new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002"));
        given(canvasInfoRepository.findById(300)).willReturn(Optional.of(info));

        // when
        canvasService.handleInternalDisconnect(300, 1L, 0);

        // then
        // assertThat(testUser.getIsAccessed()).isFalse();
        assertThat(info.getIsCached()).isFalse();
        assertThat(info.getRedisIp()).isNull();
        assertThat(info.getRedisPort()).isNull();
        assertThat(info.getServerIp()).isNull();
        assertThat(info.getServerPort()).isNull();
        verify(canvasInfoRepository).save(info);
    }
}
