package com.endpoint.frelog.domain.canvas.service;

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
import java.util.Map;
import org.mockito.ArgumentCaptor;

import static org.assertj.core.api.Assertions.assertThat;
import static org.assertj.core.api.Assertions.assertThatThrownBy;
import static org.mockito.ArgumentMatchers.anyString;
import static org.mockito.ArgumentMatchers.anyInt;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;
import static org.mockito.Mockito.verifyNoInteractions;

@ExtendWith(MockitoExtension.class)
class CanvasServiceTest {

    @Mock
    private CanvasInfoRepository canvasInfoRepository;

    @Mock
    private UserRepository userRepository;

    @Mock
    private com.endpoint.frelog.domain.user.repository.UserSessionRepository userSessionRepository;

    @Mock
    private CanvasElasticsearchService canvasElasticsearchService;

    @Mock
    private CanvasRedisDocumentReader redisDocumentReader;

    @Mock
    private CanvasResourceService canvasResourceService;

    @Mock
    private LoadBalancerService loadBalancerService;

    @Mock
    private com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository serverInfoRepository;

    @Mock
    private com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository redisInfoRepository;

    @Mock
    private com.endpoint.frelog.global.security.JwtTokenProvider jwtTokenProvider;

    @Mock private com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupQueue runtimeCleanupQueue;
    @Mock private org.springframework.context.ApplicationEventPublisher events;
    @InjectMocks
    private CanvasService canvasService;

    private User testUser;
    private CustomUserDetails userDetails;

    @BeforeEach
    void setUp() {
        testUser = new User("user@agora.com", "encodedPassword", "아고라유저", 1, Role.ROLE_USER);
        testUser.setStatus(UserStatus.ACTIVE);
        ReflectionTestUtils.setField(testUser, "userId", 1L);
        userDetails = new CustomUserDetails(testUser);
    }

    @Test
    @DisplayName("캔버스 생성 시 MS SQL과 Elasticsearch에 각각 데이터가 저장된다")
    void createCanvas_Success() {
        // given
        CanvasInfo savedInfo = new CanvasInfo(101);
        given(canvasInfoRepository.save(any(CanvasInfo.class))).willReturn(savedInfo);
        given(canvasResourceService.saveRepresentativeImage(eq(101), any())).willReturn("/api/canvases/101/image");
        given(canvasElasticsearchService.saveCanvas(any(CanvasDocument.class))).willReturn(true);

        // when
        CanvasSummaryResponse response = canvasService.createCanvas("Test Canvas", "description", "pass123", null, userDetails);

        // then
        assertThat(response).isNotNull();
        assertThat(response.canvasId()).isEqualTo(101);
        assertThat(response.canvasName()).isEqualTo("Test Canvas");
        assertThat(response.description()).isEqualTo("description");

        verify(canvasInfoRepository).save(any(CanvasInfo.class));
        ArgumentCaptor<CanvasDocument> saved = ArgumentCaptor.forClass(CanvasDocument.class);
        verify(canvasElasticsearchService).saveCanvas(saved.capture());
        assertThat(saved.getValue().getCanvasPasswordHash()).startsWith("$2").isNotEqualTo("pass123");
    }

    @Test
    @DisplayName("참여자는 닉네임과 태그로 찾아 초기 그룹에 추가한다")
    void addParticipant_UsesHandleAndInitialGroup() {
        CanvasDocument doc = new CanvasDocument("Canvas", 11, 1L, null, "default");
        given(canvasElasticsearchService.getCanvasDocumentById(11)).willReturn(Optional.of(doc));
        given(canvasInfoRepository.findByIdWithPessimisticLock(11)).willReturn(Optional.of(new CanvasInfo(11)));
        User participant = new User("other@agora.com", "hash", "동료", 7, Role.ROLE_USER);
        participant.setStatus(UserStatus.ACTIVE);
        ReflectionTestUtils.setField(participant, "userId", 2L);
        given(userRepository.findByNicknameAndTagNumber("동료", 7)).willReturn(Optional.of(participant));

        canvasService.addCanvasParticipant(11, "동료", 7, userDetails);

        ArgumentCaptor<Map> fields = ArgumentCaptor.forClass(Map.class);
        verify(canvasElasticsearchService).patchCanvasFields(eq(11), fields.capture());
        assertThat((List<Long>) fields.getValue().get("people")).containsExactly(1L, 2L);
        assertThat((Map<String, List<Long>>) fields.getValue().get("inner-group"))
                .containsEntry("default", List.of(2L));
    }

    @Test
    @DisplayName("활성 캔버스의 설정 변경은 캐시 덮어쓰기를 막기 위해 거부한다")
    void updateName_CachedCanvasRejected() {
        CanvasInfo info = new CanvasInfo(12);
        info.setIsCached(true);
        given(canvasInfoRepository.findByIdWithPessimisticLock(12)).willReturn(Optional.of(info));

        assertThatThrownBy(() -> canvasService.updateCanvasName(12, "Changed", userDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_ACTIVE);
    }

    @Test
    @DisplayName("보호된 캔버스는 비밀번호 없이는 접속 토큰을 발급하지 않는다")
    void accessCanvas_RequiresPassword() {
        CanvasDocument doc = new CanvasDocument("Protected", 13, 1L, "secret", "default");
        given(canvasElasticsearchService.getCanvasDocumentById(13)).willReturn(Optional.of(doc));
        given(canvasInfoRepository.findByIdWithPessimisticLock(13)).willReturn(Optional.of(new CanvasInfo(13)));
        jakarta.servlet.http.HttpServletRequest request = org.mockito.Mockito.mock(jakarta.servlet.http.HttpServletRequest.class);

        assertThatThrownBy(() -> canvasService.accessCanvas(13, request, userDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_PASSWORD_REQUIRED);
        assertThatThrownBy(() -> canvasService.accessCanvas(13, request, userDetails, "wrong"))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_PASSWORD_INVALID);
    }

    @Test
    @DisplayName("활성 캔버스 접속 권한은 Elasticsearch 사본이 아닌 Redis 최신 문서로 확인한다")
    void accessCanvas_ActiveUsesRedisDocument() {
        CanvasInfo info = new CanvasInfo(14);
        info.setIsCached(true);
        info.setRedisInfo(new com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo("127.0.0.1", "6379"));
        given(canvasInfoRepository.findByIdWithPessimisticLock(14)).willReturn(Optional.of(info));
        CanvasDocument live = new CanvasDocument("Live", 14, 1L, CanvasPasswords.hashForStorage("new-secret"), "default");
        given(redisDocumentReader.read(info)).willReturn(live);
        jakarta.servlet.http.HttpServletRequest request = org.mockito.Mockito.mock(jakarta.servlet.http.HttpServletRequest.class);

        assertThatThrownBy(() -> canvasService.accessCanvas(14, request, userDetails, "old-secret"))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_PASSWORD_INVALID);
        verifyNoInteractions(canvasElasticsearchService);
    }

    @Test
    @DisplayName("캔버스 검색 시 Elasticsearch에서 조회하고 CanvasSummaryResponse 목록 반환")
    void searchCanvases_Success() {
        // given
        CanvasDocument doc = new CanvasDocument("Agora Canvas", 50, 1L, null, "default");
        doc.setDescription("Agora canvas desc");
        doc.setPeople(List.of(1L, 2L));

        given(canvasElasticsearchService.searchCanvasesByName("Agora")).willReturn(List.of(doc));
        given(canvasInfoRepository.findById(50)).willReturn(Optional.of(new CanvasInfo(50)));

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
        given(canvasInfoRepository.findByIdWithPessimisticLock(10)).willReturn(Optional.of(new CanvasInfo(10)));

        // when
        CanvasSummaryResponse summary = canvasService.getCanvasSummary(10, userDetails);

        // then
        assertThat(summary).isNotNull();
        assertThat(summary.canvasId()).isEqualTo(10);
        assertThat(summary.canvasName()).isEqualTo("Solo Canvas");
        assertThat(summary.userCount()).isEqualTo(1);
    }

    @Test
    @DisplayName("비공개 캔버스는 초대받지 않은 사용자에게 검색이나 상세 조회로 노출되지 않는다")
    void privateCanvas_IsHiddenFromNonParticipants() {
        CanvasDocument hidden = new CanvasDocument("Private", 77, 2L, null, "default");
        given(canvasElasticsearchService.searchCanvasesByName("Private")).willReturn(List.of(hidden));
        given(canvasInfoRepository.findByIdWithPessimisticLock(77)).willReturn(Optional.of(new CanvasInfo(77)));
        given(canvasElasticsearchService.getCanvasDocumentById(77)).willReturn(Optional.of(hidden));
        assertThat(canvasService.searchCanvases("Private", userDetails)).isEmpty();
        assertThatThrownBy(() -> canvasService.getCanvasSummary(77, userDetails))
                .isInstanceOf(CustomException.class).hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_NOT_FOUND);
        assertThatThrownBy(() -> canvasService.accessCanvas(77, org.mockito.Mockito.mock(jakarta.servlet.http.HttpServletRequest.class), userDetails))
                .isInstanceOf(CustomException.class).hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_NOT_FOUND);
    }

    @Test
    @DisplayName("공개 캔버스는 로그인한 비참가자도 검색과 상세 조회를 할 수 있다")
    void publicCanvas_IsVisibleToSignedInUsers() {
        CanvasDocument doc = new CanvasDocument("Public", 78, 2L, null, "default"); doc.setVisibility("public");
        given(canvasElasticsearchService.searchCanvasesByName("Public")).willReturn(List.of(doc));
        given(canvasInfoRepository.findById(78)).willReturn(Optional.of(new CanvasInfo(78)));
        given(canvasInfoRepository.findByIdWithPessimisticLock(78)).willReturn(Optional.of(new CanvasInfo(78)));
        given(canvasElasticsearchService.getCanvasDocumentById(78)).willReturn(Optional.of(doc));
        assertThat(canvasService.searchCanvases("Public", userDetails)).extracting(CanvasSummaryResponse::visibility).containsExactly("public");
        assertThat(canvasService.getCanvasSummary(78, userDetails).visibility()).isEqualTo("public");
    }

    @Test
    @DisplayName("활성 캔버스 검색은 Redis의 최신 비공개 설정을 확인한다")
    void searchUsesActiveVisibility() {
        CanvasDocument stale = new CanvasDocument("Changed", 79, 2L, null, "default"); stale.setVisibility("public");
        CanvasDocument live = new CanvasDocument("Changed", 79, 2L, null, "default");
        CanvasInfo info = new CanvasInfo(79); info.setIsCached(true);
        given(canvasElasticsearchService.searchCanvasesByName("Changed")).willReturn(List.of(stale));
        given(canvasInfoRepository.findById(79)).willReturn(Optional.of(info));
        given(redisDocumentReader.read(info)).willReturn(live);
        assertThat(canvasService.searchCanvases("Changed", userDetails)).isEmpty();
    }



    @Test
    @DisplayName("캔버스 삭제 시 is_cached가 true라면 CustomException(BAD_REQUEST)을 던진다")
    void deleteCanvas_Cached_ThrowsException() {
        // given
        CanvasDocument doc = new CanvasDocument("Delete Canvas", 200, 1L, null, "default");
        given(canvasElasticsearchService.getCanvasDocumentById(200)).willReturn(Optional.of(doc));

        CanvasInfo info = new CanvasInfo(200);
        info.setIsCached(true);
        given(canvasInfoRepository.findByIdWithPessimisticLock(200)).willReturn(Optional.of(info));

        // when & then
        assertThatThrownBy(() -> canvasService.deleteCanvas(200, userDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_ACTIVE);
    }

    @Test void deleteCanvasQueuesCleanupAndPublishesOnlyAfterDocumentDeletion() {
        var doc = new CanvasDocument("Delete",201,1L,null,"default");
        var info = new CanvasInfo(201);
        given(canvasInfoRepository.findByIdWithPessimisticLock(201)).willReturn(Optional.of(info));
        given(canvasElasticsearchService.getCanvasDocumentById(201)).willReturn(Optional.of(doc));
        given(canvasElasticsearchService.deleteCanvas(201,"Delete")).willReturn(true);
        canvasService.deleteCanvas(201,userDetails);
        var order=org.mockito.Mockito.inOrder(runtimeCleanupQueue,canvasInfoRepository,canvasElasticsearchService,events);
        order.verify(runtimeCleanupQueue).enqueue(201);
        order.verify(canvasInfoRepository).delete(info);
        order.verify(canvasElasticsearchService).deleteCanvas(201,"Delete");
        order.verify(events).publishEvent(new com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupCoordinator.Deleted(201));
    }

    @Test void failedDocumentDeleteDoesNotPublishCleanupEvent() {
        var doc = new CanvasDocument("Delete",202,1L,null,"default");
        given(canvasInfoRepository.findByIdWithPessimisticLock(202)).willReturn(Optional.of(new CanvasInfo(202)));
        given(canvasElasticsearchService.getCanvasDocumentById(202)).willReturn(Optional.of(doc));
        assertThatThrownBy(()->canvasService.deleteCanvas(202,userDetails)).isInstanceOf(CustomException.class);
        verifyNoInteractions(events);
    }

    @Test
    @DisplayName("캔버스 접속 시 참여자 목록과 무관하게 미캐시 상태면 P2C로 할당하고 JWT를 C++ 서버에 등록한다")
    void accessCanvas_NotCached_AllocatesAndRegisters() {
        // given
        CanvasDocument doc = new CanvasDocument("Access Canvas", 300, 1L, "hashedPass", "default");
        given(canvasElasticsearchService.getCanvasDocumentById(300)).willReturn(Optional.of(doc));

        CanvasInfo info = new CanvasInfo(300);
        info.setIsCached(false);
        given(canvasInfoRepository.findByIdWithPessimisticLock(300)).willReturn(Optional.of(info));

        given(loadBalancerService.allocateServer()).willReturn(AllocateServerResponse.of("127.0.0.1", "8000", "8002"));

        com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo sInfo = new com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo("127.0.0.1", "8000", "8002", "Cpp-1");
        sInfo.setServerId(1);
        given(serverInfoRepository.findByServerIpAndServerPort("127.0.0.1", "8000")).willReturn(java.util.Optional.of(sInfo));

        jakarta.servlet.http.HttpServletRequest request = org.mockito.Mockito.mock(jakarta.servlet.http.HttpServletRequest.class);
        given(request.getRemoteAddr()).willReturn("192.168.0.100");
        
        given(jwtTokenProvider.createCanvasAccessToken(
                anyString(), anyInt(), eq(300), eq("192.168.0.100"), anyString(), eq(0L)
        )).willReturn("mock-canvas-token");

        // when
        CanvasUpdateDtos.AccessResponse response = canvasService.accessCanvas(300, request, userDetails, "hashedPass");

        // then
        assertThat(response).isNotNull();
        assertThat(response.serverId()).isEqualTo(1);
        assertThat(response.wsPort()).isEqualTo("8002");
        assertThat(response.canvasAccessToken()).isEqualTo("mock-canvas-token");
        verifyNoInteractions(userSessionRepository);
        // assertThat(info.getIsCached()).isTrue();
        // assertThat(testUser.getIsAccessed()).isTrue();
    }

}
