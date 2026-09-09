package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CreateCanvasRequest;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
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
import static org.mockito.BDDMockito.given;
import static org.mockito.Mockito.verify;

@ExtendWith(MockitoExtension.class)
class CanvasServiceTest {

    @Mock
    private CanvasInfoRepository canvasInfoRepository;

    @Mock
    private UserRepository userRepository;

    @InjectMocks
    private CanvasService canvasService;

    private User testUser;

    @BeforeEach
    void setUp() {
        testUser = new User("user@agora.com", "encodedPassword", "아고라유저", Role.ROLE_USER);
        testUser.setStatus(UserStatus.ACTIVE);
        ReflectionTestUtils.setField(testUser, "userId", 1L);
    }

    @Test
    @DisplayName("캔버스 생성 시 redis와 server는 항상 none(null), is_cached는 false로 초기화되며 user_id가 연동됨")
    void createCanvas_InitialCacheState_NoneAndFalse_WithUser() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Agora Shared Canvas", 1001, 1L);
        given(canvasInfoRepository.existsByCanvasName(request.canvasName())).willReturn(false);
        given(canvasInfoRepository.existsById(1001)).willReturn(false);
        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));

        CanvasInfo savedEntity = new CanvasInfo(1001, "Agora Shared Canvas", testUser);
        savedEntity.setRedisIp(null);
        savedEntity.setRedisPort(null);
        savedEntity.setServerIp(null);
        savedEntity.setServerPort(null);
        savedEntity.setIsCached(false);

        given(canvasInfoRepository.save(any(CanvasInfo.class))).willReturn(savedEntity);

        // when
        CanvasResponse response = canvasService.createCanvas(request);

        // then
        assertThat(response).isNotNull();
        assertThat(response.canvasId()).isEqualTo(1001);
        assertThat(response.canvasName()).isEqualTo("Agora Shared Canvas");
        assertThat(response.userId()).isEqualTo(1L);
        assertThat(response.userNickname()).isEqualTo("아고라유저");
        assertThat(response.redisIp()).isNull();
        assertThat(response.redisPort()).isNull();
        assertThat(response.serverIp()).isNull();
        assertThat(response.serverPort()).isNull();
        assertThat(response.isCached()).isFalse();

        verify(canvasInfoRepository).save(any(CanvasInfo.class));
    }

    @Test
    @DisplayName("캔버스 생성 시 유저가 존재하지 않으면 USER_NOT_FOUND 예외 발생")
    void createCanvas_UserNotFound_ThrowsException() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("No User Canvas", 1002, 999L);
        given(canvasInfoRepository.existsByCanvasName(request.canvasName())).willReturn(false);
        given(userRepository.findById(999L)).willReturn(Optional.empty());

        // when & then
        assertThatThrownBy(() -> canvasService.createCanvas(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.USER_NOT_FOUND);
    }

    @Test
    @DisplayName("canvasId 미입력 시 max(canvas_id) + 1로 자동 채번")
    void createCanvas_AutoGenerateId() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Auto Id Canvas", null, 1L);
        given(canvasInfoRepository.existsByCanvasName(request.canvasName())).willReturn(false);
        given(userRepository.findById(1L)).willReturn(Optional.of(testUser));
        given(canvasInfoRepository.findMaxCanvasId()).willReturn(10);

        CanvasInfo savedEntity = new CanvasInfo(11, "Auto Id Canvas", testUser);
        given(canvasInfoRepository.save(any(CanvasInfo.class))).willReturn(savedEntity);

        // when
        CanvasResponse response = canvasService.createCanvas(request);

        // then
        assertThat(response.canvasId()).isEqualTo(11);
        assertThat(response.canvasName()).isEqualTo("Auto Id Canvas");
        assertThat(response.userId()).isEqualTo(1L);
        assertThat(response.isCached()).isFalse();
    }

    @Test
    @DisplayName("중복된 캔버스 이름으로 생성 시 예외 발생")
    void createCanvas_DuplicateName_ThrowsException() {
        // given
        CreateCanvasRequest request = new CreateCanvasRequest("Duplicate Canvas", null, 1L);
        given(canvasInfoRepository.existsByCanvasName(request.canvasName())).willReturn(true);

        // when & then
        assertThatThrownBy(() -> canvasService.createCanvas(request))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_ALREADY_EXISTS);
    }

    @Test
    @DisplayName("캔버스 단건 조회 성공")
    void getCanvas_Success() {
        // given
        CanvasInfo entity = new CanvasInfo(500, "Test Canvas", testUser);
        given(canvasInfoRepository.findById(500)).willReturn(Optional.of(entity));

        // when
        CanvasResponse response = canvasService.getCanvas(500);

        // then
        assertThat(response.canvasId()).isEqualTo(500);
        assertThat(response.canvasName()).isEqualTo("Test Canvas");
        assertThat(response.userId()).isEqualTo(1L);
        assertThat(response.userNickname()).isEqualTo("아고라유저");
    }

    @Test
    @DisplayName("존재하지 않는 캔버스 조회 시 예외 발생")
    void getCanvas_NotFound_ThrowsException() {
        // given
        given(canvasInfoRepository.findById(999)).willReturn(Optional.empty());

        // when & then
        assertThatThrownBy(() -> canvasService.getCanvas(999))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.CANVAS_NOT_FOUND);
    }

    @Test
    @DisplayName("전체 캔버스 목록 조회")
    void listCanvases_Success() {
        // given
        CanvasInfo c1 = new CanvasInfo(1, "Canvas 1", testUser);
        CanvasInfo c2 = new CanvasInfo(2, "Canvas 2", testUser);
        given(canvasInfoRepository.findAll()).willReturn(List.of(c1, c2));

        // when
        List<CanvasResponse> list = canvasService.listCanvases();

        // then
        assertThat(list).hasSize(2);
        assertThat(list.get(0).canvasName()).isEqualTo("Canvas 1");
        assertThat(list.get(1).canvasName()).isEqualTo("Canvas 2");
    }

    @Test
    @DisplayName("특정 유저의 캔버스 목록 조회")
    void listCanvasesByUser_Success() {
        // given
        CanvasInfo c1 = new CanvasInfo(1, "User Canvas 1", testUser);
        given(canvasInfoRepository.findByUser_UserId(1L)).willReturn(List.of(c1));

        // when
        List<CanvasResponse> list = canvasService.listCanvasesByUserId(1L);

        // then
        assertThat(list).hasSize(1);
        assertThat(list.get(0).canvasName()).isEqualTo("User Canvas 1");
        assertThat(list.get(0).userId()).isEqualTo(1L);
    }

    @Test
    @DisplayName("캔버스 소유자가 캐시 정보 업데이트 시 성공")
    void updateCanvasCache_Success_ByOwner() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Cached Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));
        given(canvasInfoRepository.save(any(CanvasInfo.class))).willAnswer(invocation -> invocation.getArgument(0));

        com.endpoint.frelog.global.security.CustomUserDetails ownerDetails = new com.endpoint.frelog.global.security.CustomUserDetails(testUser);
        UpdateCanvasCacheRequest updateReq = new UpdateCanvasCacheRequest(
                true, "127.0.0.1", "6379", "127.0.0.1", "8000"
        );

        // when
        CanvasResponse response = canvasService.updateCanvasCache(100, updateReq, ownerDetails);

        // then
        assertThat(response.isCached()).isTrue();
        assertThat(response.redisIp()).isEqualTo("127.0.0.1");
        assertThat(response.redisPort()).isEqualTo("6379");
    }

    @Test
    @DisplayName("관리자(ROLE_ADMIN) 계정이 다른 사용자의 캔버스 캐시 정보 업데이트 시 성공")
    void updateCanvasCache_Success_ByAdmin() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Cached Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));
        given(canvasInfoRepository.save(any(CanvasInfo.class))).willAnswer(invocation -> invocation.getArgument(0));

        User adminUser = new User("admin@agora.com", "pass", "관리자", Role.ROLE_ADMIN);
        ReflectionTestUtils.setField(adminUser, "userId", 99L);
        com.endpoint.frelog.global.security.CustomUserDetails adminDetails = new com.endpoint.frelog.global.security.CustomUserDetails(adminUser);

        UpdateCanvasCacheRequest updateReq = new UpdateCanvasCacheRequest(
                true, "10.0.0.1", "6379", "10.0.0.1", "8000"
        );

        // when
        CanvasResponse response = canvasService.updateCanvasCache(100, updateReq, adminDetails);

        // then
        assertThat(response.isCached()).isTrue();
        assertThat(response.redisIp()).isEqualTo("10.0.0.1");
    }

    @Test
    @DisplayName("소유자도 아니고 관리자도 아닌 일반 사용자가 캔버스 캐시 수정 시 ACCESS_DENIED 예외 발생")
    void updateCanvasCache_AccessDenied_ByOtherUser() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Cached Canvas", testUser); // owner userId: 1
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        User otherUser = new User("other@agora.com", "pass", "타인", Role.ROLE_USER);
        ReflectionTestUtils.setField(otherUser, "userId", 2L); // other userId: 2
        com.endpoint.frelog.global.security.CustomUserDetails otherDetails = new com.endpoint.frelog.global.security.CustomUserDetails(otherUser);

        UpdateCanvasCacheRequest updateReq = new UpdateCanvasCacheRequest(true, "127.0.0.1", "6379", null, null);

        // when & then
        assertThatThrownBy(() -> canvasService.updateCanvasCache(100, updateReq, otherDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.ACCESS_DENIED);
    }

    @Test
    @DisplayName("비로그인 상태에서 캔버스 캐시 수정 시 UNAUTHORIZED 예외 발생")
    void updateCanvasCache_Unauthorized_WhenNoUser() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Cached Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        UpdateCanvasCacheRequest updateReq = new UpdateCanvasCacheRequest(true, "127.0.0.1", "6379", null, null);

        // when & then
        assertThatThrownBy(() -> canvasService.updateCanvasCache(100, updateReq, null))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.UNAUTHORIZED);
    }

    @Test
    @DisplayName("캔버스 소유자가 삭제 시 성공")
    void deleteCanvas_Success_ByOwner() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Delete Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        com.endpoint.frelog.global.security.CustomUserDetails ownerDetails = new com.endpoint.frelog.global.security.CustomUserDetails(testUser);

        // when
        canvasService.deleteCanvas(100, ownerDetails);

        // then
        verify(canvasInfoRepository).delete(entity);
    }

    @Test
    @DisplayName("관리자(ROLE_ADMIN)가 캔버스 삭제 시 성공")
    void deleteCanvas_Success_ByAdmin() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Delete Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        User adminUser = new User("admin@agora.com", "pass", "관리자", Role.ROLE_ADMIN);
        ReflectionTestUtils.setField(adminUser, "userId", 99L);
        com.endpoint.frelog.global.security.CustomUserDetails adminDetails = new com.endpoint.frelog.global.security.CustomUserDetails(adminUser);

        // when
        canvasService.deleteCanvas(100, adminDetails);

        // then
        verify(canvasInfoRepository).delete(entity);
    }

    @Test
    @DisplayName("소유자도 아니고 관리자도 아닌 일반 사용자가 캔버스 삭제 시 ACCESS_DENIED 예외 발생")
    void deleteCanvas_AccessDenied_ByOtherUser() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Delete Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        User otherUser = new User("other@agora.com", "pass", "타인", Role.ROLE_USER);
        ReflectionTestUtils.setField(otherUser, "userId", 2L);
        com.endpoint.frelog.global.security.CustomUserDetails otherDetails = new com.endpoint.frelog.global.security.CustomUserDetails(otherUser);

        // when & then
        assertThatThrownBy(() -> canvasService.deleteCanvas(100, otherDetails))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.ACCESS_DENIED);
    }

    @Test
    @DisplayName("비로그인 상태에서 캔버스 삭제 시 UNAUTHORIZED 예외 발생")
    void deleteCanvas_Unauthorized_WhenNoUser() {
        // given
        CanvasInfo entity = new CanvasInfo(100, "Delete Canvas", testUser);
        given(canvasInfoRepository.findById(100)).willReturn(Optional.of(entity));

        // when & then
        assertThatThrownBy(() -> canvasService.deleteCanvas(100, null))
                .isInstanceOf(CustomException.class)
                .hasFieldOrPropertyWithValue("errorCode", ErrorCode.UNAUTHORIZED);
    }
}
