package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasSummaryResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos;
import com.endpoint.frelog.domain.canvas.dto.UpdateCanvasCacheRequest;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.multipart.MultipartFile;

import java.util.ArrayList;
import java.util.HashSet;
import java.util.List;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.util.Set;

import org.springframework.beans.factory.annotation.Value;
import org.springframework.stereotype.Service;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;

@Service
public class CanvasService {

    private static final Logger log = LoggerFactory.getLogger(CanvasService.class);

    private final CanvasInfoRepository canvasInfoRepository;
    private final UserRepository userRepository;
    private final CanvasElasticsearchService canvasElasticsearchService;
    private final CanvasResourceService canvasResourceService;
    private final CppServerClient cppServerClient;
    private final LoadBalancerService loadBalancerService;
    private final RedisInfoRepository redisInfoRepository;
    private final ServerInfoRepository serverInfoRepository;
    private final UserSessionRepository userSessionRepository;
    private final JwtTokenProvider jwtTokenProvider;

    public CanvasService(
            CanvasInfoRepository canvasInfoRepository,
            UserRepository userRepository,
            CanvasElasticsearchService canvasElasticsearchService,
            CanvasResourceService canvasResourceService,
            CppServerClient cppServerClient,
            LoadBalancerService loadBalancerService,
            RedisInfoRepository redisInfoRepository,
            ServerInfoRepository serverInfoRepository,
            UserSessionRepository userSessionRepository,
            JwtTokenProvider jwtTokenProvider) {
        this.canvasInfoRepository = canvasInfoRepository;
        this.userRepository = userRepository;
        this.canvasElasticsearchService = canvasElasticsearchService;
        this.canvasResourceService = canvasResourceService;
        this.cppServerClient = cppServerClient;
        this.loadBalancerService = loadBalancerService;
        this.redisInfoRepository = redisInfoRepository;
        this.serverInfoRepository = serverInfoRepository;
        this.userSessionRepository = userSessionRepository;
        this.jwtTokenProvider = jwtTokenProvider;
    }

    /**
     * 1. 캔버스 생성:
     * - 로그인한 사용자만 허용
     * - MS SQL에 새로운 캔버스 행 데이터를 생성
     * - 그 id를 기반으로 엘라스틱서치의 초기 캔버스 문서를 생성
     * - 비정형 데이터(대표 이미지 등)는 ~/project-agora/canvas-resource/{canvas-id} 에 저장
     */
    @Transactional
    public CanvasSummaryResponse createCanvas(
            String canvasName,
            String description,
            String canvasPassword,
            MultipartFile imageFile,
            CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "캔버스 생성은 로그인한 사용자만 가능합니다.");
        }

        Long userId = currentUser.getUserId();

        // 1. MS SQL canvas_id 채번 (IDENTITY 자동 증가)
        CanvasInfo canvasInfo = new CanvasInfo();
        canvasInfo = canvasInfoRepository.save(canvasInfo);
        Integer targetId = canvasInfo.getCanvasId();

        // 2. 비정형 대표 이미지 저장
        String imagePath = canvasResourceService.saveRepresentativeImage(targetId, imageFile);

        // 3. Elasticsearch 초기 문서 생성
        CanvasDocument document = new CanvasDocument(
                canvasName,
                targetId,
                userId,
                canvasPassword,
                "default"
        );
        document.setDescription(description != null ? description : "");
        canvasElasticsearchService.saveCanvas(document);

        log.info("신규 캔버스 #{} 생성 완료: name='{}', admin_user_id={}", targetId, canvasName, userId);
        return CanvasSummaryResponse.of(document, imagePath);
    }

    /**
     * 2. 캔버스 검색:
     * - 로그인한 사용자만 허용
     * - 캔버스 이름을 기반으로 검색되며 엘라스틱서치로부터 정보를 가져옴
     * - 캔버스 아이디, 대표 이미지, 설명 텍스트, 캔버스 이름, 이용자 수만 제공
     * - MS SQL의 캔버스 정보 테이블은 그 어떤 정보도 제공하지 않음
     */
    public List<CanvasSummaryResponse> searchCanvases(String canvasName, CustomUserDetails currentUser) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        List<CanvasDocument> docs = canvasElasticsearchService.searchCanvasesByName(canvasName);
        return docs.stream()
                .map(doc -> CanvasSummaryResponse.of(doc, "/api/canvases/" + doc.getCanvasId() + "/image"))
                .toList();
    }

    /**
     * 3. 캔버스 조회:
     * - 로그인한 사용자만 허용
     * - 캔버스 아이디를 기반으로 조회를 수행하며 제공하는 데이터는 캔버스 검색과 동일
     */
    public CanvasSummaryResponse getCanvasSummary(Integer canvasId, CustomUserDetails currentUser) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        CanvasDocument doc = canvasElasticsearchService.getCanvasDocumentById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));

        return CanvasSummaryResponse.of(doc, "/api/canvases/" + canvasId + "/image");
    }

    /**
     * 캔버스 문서 원본 단건 조회 (내부 및 도큐먼트 검증용)
     */
    public CanvasDocument getCanvasDocument(Integer canvasId, CustomUserDetails currentUser) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        return canvasElasticsearchService.getCanvasDocumentById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));
    }

    // =========================================================================
    // 4. 캔버스 변경 API (어드민 그룹 또는 시스템 관리자 전용)
    // =========================================================================



    /**
     * 4.5 캔버스 삭제:
     * - 캔버스 소유 사용자(admin-user-id) 또는 관리자만 가능
     * - C++ 서버의 API를 통해 모든 사용자의 접속을 중단하고 MS SQL, Elasticsearch, Redis에서 해당 캔버스를 삭제, 단 is_cached가 true인 경우에만
     * - ~/project-agora/canvas-resource/{canvas-id} 디렉터리 정리
     */
    @Transactional
    public void deleteCanvas(Integer canvasId, CustomUserDetails currentUser) {
        CanvasDocument doc = getCanvasDocumentOrThrow(canvasId);
        validateCanvasOwnerOrSystemAdmin(doc, currentUser);

        CanvasInfo canvasInfo = getCanvasInfoWithLockOrThrow(canvasId);
        boolean isCached = Boolean.TRUE.equals(canvasInfo.getIsCached());

        if (isCached) {
            log.warn("캔버스 #{} 삭제 실패: 활성화(캐시) 상태인 캔버스는 삭제할 수 없습니다.", canvasId);
            throw new CustomException(ErrorCode.BAD_REQUEST, "현재 활성화 상태인 캔버스는 삭제할 수 없습니다. 모든 사용자가 연결을 종료한 후 다시 시도해 주세요.");
        }

        if (userSessionRepository.existsByCanvas_CanvasIdAndIsAccessedTrue(canvasId)) {
            throw new CustomException(ErrorCode.BAD_REQUEST, "접속 중인 사용자가 있어 캔버스를 삭제할 수 없습니다.");
        }
        userSessionRepository.clearInactiveCanvasReferences(canvasId);

        // MS SQL 삭제
        canvasInfoRepository.delete(canvasInfo);

        // Elasticsearch 삭제
        canvasElasticsearchService.deleteCanvas(canvasId, doc.getCanvasName());

        // 비정형 리소스 디렉터리 정리
        canvasResourceService.deleteCanvasResourceDirectory(canvasId);

        log.info("캔버스 #{} 전체 영구 삭제 완료", canvasId);
    }

    // =========================================================================
    // 5. Access API
    // =========================================================================

    /**
     * 5. Access API:
     * - 특정 캔버스 아이디에 대한 접속 API
     * - 로그인한 사용자만 허용하고 그 중에서도 초대된 사용자 리스트(people)에 속한 경우에만 허용
     * - is_cached가 true: 해당 테이블의 redis, server 정보 반환
     * - is_cached가 false: redis 및 server 정보 테이블에서 is_activated가 true인 행에 대해서만 로드 밸런싱 수행 후 canvas_info 업데이트 및 is_cached=true 설정
     * - 해당 사용자의 JWT 토큰을 C++ 서버의 API를 통해 등록
     * - user 테이블의 is_accessed, server_ip, server_port 갱신
     * - 로드 밸런싱된 C++ 서버의 아이피와 포트만 반환
     */
    @Transactional
    public CanvasUpdateDtos.AccessResponse accessCanvas(Integer canvasId, jakarta.servlet.http.HttpServletRequest httpRequest, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        Long userId = currentUser.getUserId();

        // 1. 캔버스 도큐먼트 조회 및 people 목록 검증
        CanvasDocument doc = getCanvasDocumentOrThrow(canvasId);
        if (doc.getPeople() == null || !doc.getPeople().contains(userId)) {
            throw new CustomException(ErrorCode.ACCESS_DENIED, "초대된 사용자 리스트(people)에 속한 경우에만 접근할 수 있습니다.");
        }

        // 2. MS SQL canvas_info 확인 (동시 삭제/할당 방지를 위한 비관적 락 사용)
        CanvasInfo canvasInfo = getCanvasInfoWithLockOrThrow(canvasId);

        String serverIp = "none";
        String wsPort = "none";
        Integer serverId = null;

        ServerInfo assignedServer = canvasInfo.getCppServer();
        boolean assignedServerHealthy = Boolean.TRUE.equals(canvasInfo.getIsCached())
                && assignedServer != null
                && Boolean.TRUE.equals(assignedServer.getIsActivated())
                && assignedServer.getLastHeartbeatAt() != null
                && assignedServer.getLastHeartbeatAt().isAfter(LocalDateTime.now(ZoneOffset.UTC).minusSeconds(15))
                && cppServerClient.isHealthy(assignedServer.getServerIp(), assignedServer.getServerPort());

        if (!assignedServerHealthy) {
            if (assignedServer != null) {
                log.warn("캔버스 #{}의 기존 C++ 서버 #{}가 응답하지 않아 할당을 해제합니다.", canvasId, assignedServer.getServerId());
                // Redis 캐시는 유지하여 새 C++ 서버가 최신 상태를 인계받게 한다.
                canvasInfo.setCppServer(null);
                canvasInfoRepository.save(canvasInfo);
            }
            AllocateServerResponse serverAlloc = loadBalancerService.allocateServer();
            ServerInfo sInfo = serverInfoRepository.findByServerIpAndServerPort(serverAlloc.serverIp(), serverAlloc.serverPort()).orElse(null);
            
            if (sInfo != null) {
                serverIp = sInfo.getServerIp();
                wsPort = sInfo.getWsPort();
                serverId = sInfo.getServerId();
            }
            log.info("캔버스 #{} 신규 접속 요청: 클라이언트에게 접속할 C++ 서버 안내 완료 [Server={}:{}] (실제 캐시 할당은 웹소켓 연결 시 처리됨)",
                    canvasId, serverIp, wsPort);
        } else {
            serverIp = assignedServer.getServerIp();
            wsPort = assignedServer.getWsPort();
            serverId = assignedServer.getServerId();
        }

        // 3. 중복 접속 검사 (user_sessions 테이블)
        UserSession session = userSessionRepository.findById(userId).orElseGet(() -> new UserSession(userRepository.findById(userId).orElseThrow()));
        if (Boolean.TRUE.equals(session.getIsAccessed())
                && (session.getCanvas() == null || !canvasId.equals(session.getCanvas().getCanvasId()))) {
            throw new CustomException(ErrorCode.ALREADY_CONNECTED, "이미 캔버스에 접속 중인 사용자입니다. (다중 탭 접속 차단)");
        }

        // 4. user_sessions 테이블 상태 갱신 (접속 중 상태로 기록)은 C++ 서버로 이관됨
        // session.setIsAccessed(true);
        // session.setCppServer(canvasInfo.getCppServer());
        // session.setCanvas(canvasInfo);
        // userSessionRepository.save(session);

        // 5. C++ 실시간 서버 전용 JWT (해시 및 tagNumber 포함) 발급
        String serverHash = "none";
        if (!"none".equals(serverIp) && !"none".equals(wsPort)) {
            try {
                String raw = serverIp + ":" + wsPort;
                java.security.MessageDigest digest = java.security.MessageDigest.getInstance("SHA-256");
                byte[] hash = digest.digest(raw.getBytes(java.nio.charset.StandardCharsets.UTF_8));
                serverHash = java.util.HexFormat.of().formatHex(hash);
            } catch (java.security.NoSuchAlgorithmException e) {
                log.error("Failed to generate server hash", e);
            }
        }
        
        String canvasAccessToken = jwtTokenProvider.createCanvasAccessToken(
                currentUser.getUser().getNickname(),
                currentUser.getUser().getTagNumber(),
                canvasId,
                httpRequest.getRemoteAddr(), 
                serverHash
        );

        // 6. C++ 실시간 서버의 ID, Port 및 Access Token 반환
        return new CanvasUpdateDtos.AccessResponse(serverId, wsPort, canvasAccessToken);
    }

    /**
     * 실시간 소켓/웹소켓 접속 중단 (POST /api/access/disconnect)
     */
    @Transactional
    public void disconnectCanvasAccess(Integer canvasId, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }
        Long userId = currentUser.getUserId();

        UserSession session = userSessionRepository.findById(userId).orElse(null);
        String serverIp = (session != null && session.getCppServer() != null) ? session.getCppServer().getServerIp() : null;
        String serverPort = (session != null && session.getCppServer() != null) ? session.getCppServer().getServerPort() : null;

        if ((serverIp == null || serverPort == null) && canvasId != null) {
            canvasInfoRepository.findById(canvasId).ifPresent(info -> {
                if (info.getCppServer() != null) {
                    cppServerClient.disconnectUserFromCanvas(info.getCppServer().getServerIp(), info.getCppServer().getServerPort(), canvasId, userId);
                }
            });
        } else if (serverIp != null && serverPort != null) {
            if (canvasId != null) {
                cppServerClient.disconnectUserFromCanvas(serverIp, serverPort, canvasId, userId);
            } else {
                cppServerClient.disconnectUser(serverIp, serverPort, userId);
            }
        }

        if (session != null) {
            session.setIsAccessed(false);
            session.setCppServer(null);
            session.setCanvas(null);
            userSessionRepository.save(session);
        }
        log.info("사용자 #{} 캔버스 #{} 실시간 접속 해제 완료", userId, canvasId);
    }



    /**
     * 캔버스 캐시 상태 및 할당 정보 업데이트 (PATCH /api/canvases/{canvasId}/cache)
     */
    @Transactional
    public CanvasResponse updateCanvasCache(Integer canvasId, UpdateCanvasCacheRequest request, CustomUserDetails currentUser) {
        CanvasDocument doc = getCanvasDocumentOrThrow(canvasId);
        validateCanvasAdminGroupOrSystemAdmin(doc, currentUser);

        CanvasInfo canvasInfo = getCanvasInfoOrThrow(canvasId);
        Boolean isCached = request != null ? request.isCached() : null;
        String redisIp = request != null ? request.redisIp() : null;
        String redisPort = request != null ? request.redisPort() : null;
        String serverIp = request != null ? request.serverIp() : null;
        String serverPort = request != null ? request.serverPort() : null;

        if (Boolean.FALSE.equals(isCached) || "none".equalsIgnoreCase(redisIp) || "none".equalsIgnoreCase(serverIp)) {
            canvasInfo.setIsCached(false);
            canvasInfo.setRedisInfo(null);
            canvasInfo.setCppServer(null);
        } else {
            RedisInfo rInfo = redisInfoRepository.findByRedisIpAndRedisPort(redisIp, redisPort).orElse(null);
            ServerInfo sInfo = serverInfoRepository.findByServerIpAndServerPort(serverIp, serverPort).orElse(null);
            canvasInfo.updateCacheState(isCached, rInfo, sInfo);
        }

        CanvasInfo saved = canvasInfoRepository.save(canvasInfo);
        log.info("캔버스 #{} 캐시 상태 갱신 완료: isCached={}, Server={}, Redis={}",
                canvasId, saved.getIsCached(), 
                saved.getCppServer() != null ? saved.getCppServer().getServerIp() + ":" + saved.getCppServer().getServerPort() : "none", 
                saved.getRedisInfo() != null ? saved.getRedisInfo().getRedisIp() + ":" + saved.getRedisInfo().getRedisPort() : "none");

        return CanvasResponse.from(saved);
    }

    // =========================================================================
    // 유틸리티 및 권한 검증 메서드
    // =========================================================================

    private CanvasDocument getCanvasDocumentOrThrow(Integer canvasId) {
        return canvasElasticsearchService.getCanvasDocumentById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스를 찾을 수 없습니다: " + canvasId));
    }

    private CanvasInfo getCanvasInfoOrThrow(Integer canvasId) {
        return canvasInfoRepository.findById(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스 메타데이터를 찾을 수 없습니다: " + canvasId));
    }

    private CanvasInfo getCanvasInfoWithLockOrThrow(Integer canvasId) {
        return canvasInfoRepository.findByIdWithPessimisticLock(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스 메타데이터를 찾을 수 없습니다: " + canvasId));
    }

    private void validateCanvasAdminGroupOrSystemAdmin(CanvasDocument doc, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        boolean isSystemAdmin = currentUser.getUser() != null && currentUser.getUser().getRole() == Role.ROLE_ADMIN
                || currentUser.getAuthorities().stream().anyMatch(a -> a.getAuthority().equals("ROLE_ADMIN"));

        List<Long> adminGroup = doc.getInnerGroup() != null ? doc.getInnerGroup().get("admin-group") : null;
        boolean isInAdminGroup = adminGroup != null && adminGroup.contains(currentUser.getUserId());

        if (!isSystemAdmin && !isInAdminGroup) {
            throw new CustomException(ErrorCode.ACCESS_DENIED, "캔버스 관리자 그룹(admin-group) 또는 시스템 관리자만 수정할 수 있습니다.");
        }
    }

    private void validateCanvasOwnerOrSystemAdmin(CanvasDocument doc, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        boolean isSystemAdmin = currentUser.getUser() != null && currentUser.getUser().getRole() == Role.ROLE_ADMIN
                || currentUser.getAuthorities().stream().anyMatch(a -> a.getAuthority().equals("ROLE_ADMIN"));

        boolean isOwner = doc.getAdminUserId() != null && doc.getAdminUserId().equals(currentUser.getUserId());

        if (!isSystemAdmin && !isOwner) {
            throw new CustomException(ErrorCode.ACCESS_DENIED, "캔버스 소유자(admin-user-id) 또는 시스템 관리자만 삭제할 수 있습니다.");
        }
    }
}
