package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.CanvasSummaryResponse;
import com.endpoint.frelog.domain.canvas.dto.CanvasUpdateDtos;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.entity.SpaceInfo;
import com.endpoint.frelog.domain.canvas.entity.WorkspaceInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.repository.WorkspaceInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.entity.UserStatus;
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
import java.util.List;
import java.util.LinkedHashMap;
import java.util.Optional;
import java.util.Map;
import java.time.LocalDateTime;
import java.time.ZoneOffset;
import java.nio.charset.StandardCharsets;

@Service
public class CanvasService {

    private static final Logger log = LoggerFactory.getLogger(CanvasService.class);

    private final CanvasInfoRepository canvasInfoRepository;
    private final WorkspaceInfoRepository workspaceInfoRepository;
    private final UserRepository userRepository;
    private final CanvasElasticsearchService canvasElasticsearchService;
    private final CanvasResourceService canvasResourceService;
    private final LoadBalancerService loadBalancerService;
    private final RedisInfoRepository redisInfoRepository;
    private final ServerInfoRepository serverInfoRepository;
    private final UserSessionRepository userSessionRepository;
    private final JwtTokenProvider jwtTokenProvider;
    private final CanvasRedisDocumentReader redisDocumentReader;
    private final com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupQueue runtimeCleanupQueue;
    private final org.springframework.context.ApplicationEventPublisher events;

    @org.springframework.beans.factory.annotation.Autowired
    public CanvasService(
            CanvasInfoRepository canvasInfoRepository,
            WorkspaceInfoRepository workspaceInfoRepository,
            UserRepository userRepository,
            CanvasElasticsearchService canvasElasticsearchService,
            CanvasResourceService canvasResourceService,
            LoadBalancerService loadBalancerService,
            RedisInfoRepository redisInfoRepository,
            ServerInfoRepository serverInfoRepository,
            UserSessionRepository userSessionRepository,
            JwtTokenProvider jwtTokenProvider,
            CanvasRedisDocumentReader redisDocumentReader,
            com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupQueue runtimeCleanupQueue,
            org.springframework.context.ApplicationEventPublisher events) {
        this.canvasInfoRepository = canvasInfoRepository;
        this.workspaceInfoRepository = workspaceInfoRepository;
        this.userRepository = userRepository;
        this.canvasElasticsearchService = canvasElasticsearchService;
        this.canvasResourceService = canvasResourceService;
        this.loadBalancerService = loadBalancerService;
        this.redisInfoRepository = redisInfoRepository;
        this.serverInfoRepository = serverInfoRepository;
        this.userSessionRepository = userSessionRepository;
        this.jwtTokenProvider = jwtTokenProvider;
        this.redisDocumentReader = redisDocumentReader;
        this.runtimeCleanupQueue = runtimeCleanupQueue;
        this.events = events;
    }

    public CanvasService(
            CanvasInfoRepository canvasInfoRepository,
            UserRepository userRepository,
            CanvasElasticsearchService canvasElasticsearchService,
            CanvasResourceService canvasResourceService,
            LoadBalancerService loadBalancerService,
            RedisInfoRepository redisInfoRepository,
            ServerInfoRepository serverInfoRepository,
            UserSessionRepository userSessionRepository,
            JwtTokenProvider jwtTokenProvider,
            CanvasRedisDocumentReader redisDocumentReader,
            com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupQueue runtimeCleanupQueue,
            org.springframework.context.ApplicationEventPublisher events) {
        this(canvasInfoRepository, null, userRepository, canvasElasticsearchService, canvasResourceService,
                loadBalancerService, redisInfoRepository, serverInfoRepository, userSessionRepository,
                jwtTokenProvider, redisDocumentReader, runtimeCleanupQueue, events);
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
        return createCanvas(canvasName, description, canvasPassword, imageFile, currentUser, null);
    }

    @Transactional
    public CanvasSummaryResponse createCanvas(
            String canvasName, String description, String canvasPassword, MultipartFile imageFile,
            CustomUserDetails currentUser, String visibility) {
        return createSpace(DocumentScope.CANVAS, canvasName, description, canvasPassword, imageFile, currentUser, visibility);
    }

    @Transactional
    public CanvasSummaryResponse createSpace(
            DocumentScope scope, String name, String description, String password, MultipartFile imageFile,
            CustomUserDetails currentUser, String visibility) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "생성은 로그인한 사용자만 가능합니다.");
        }
        if (name == null || name.isBlank() || name.length() > 255) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "이름은 1자 이상 255자 이하여야 합니다.");
        }
        if (description != null && description.length() > 4000) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "설명은 최대 4000자까지 가능합니다.");
        }
        if (password != null && password.getBytes(StandardCharsets.UTF_8).length > 72) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "비밀번호는 UTF-8 기준 72바이트 이하여야 합니다.");
        }
        if (visibility != null && !visibility.equals("public") && !visibility.equals("private"))
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "공개 설정을 확인하세요.");
        if (!"public".equals(visibility) && password != null && !password.isBlank())
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "비공개 공간에는 비밀번호를 설정할 수 없습니다.");

        Long userId = currentUser.getUserId();

        // 1. MS SQL ID 채번 (canvas_info / workspace_info 분리)
        SpaceInfo info = createSpaceInfo(scope);
        Integer targetId = info.getSpaceId();

        // 2. 비정형 대표 이미지 저장
        String imagePath = scope == DocumentScope.CANVAS
                ? canvasResourceService.saveRepresentativeImage(targetId, imageFile)
                : canvasResourceService.saveRepresentativeImage(scope, targetId, imageFile);

        // 3. Elasticsearch 초기 문서 생성 (canvas / workspace 인덱스 분리)
        CanvasDocument document = new CanvasDocument(
                name,
                targetId,
                userId,
                CanvasPasswords.hashForStorage(password),
                "default"
        );
        document.setDescription(description != null ? description : "");
        document.setVisibility(visibility);
        try {
            boolean saved = scope == DocumentScope.CANVAS
                    ? canvasElasticsearchService.saveCanvas(document)
                    : canvasElasticsearchService.saveDocument(scope, document);
            if (!saved) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "저장에 실패했습니다.");
            }
        } catch (RuntimeException e) {
            if (scope == DocumentScope.CANVAS) {
                canvasResourceService.deleteCanvasResourceDirectory(targetId);
            } else {
                canvasResourceService.deleteResourceDirectory(scope, targetId);
            }
            throw e;
        }

        log.info("신규 {} #{} 생성 완료: name='{}', admin_user_id={}", scope.wireName(), targetId, name, userId);
        return CanvasSummaryResponse.of(document, imagePath);
    }

    /** Branch rooms start empty; contents enter through the normal authenticated broker. */
    @Transactional
    public CanvasSummaryResponse createBranchCanvas(Integer parentId, String branch, CustomUserDetails user) {
        CanvasInfo parentInfo = getCanvasInfoWithLockOrThrow(parentId);
        CanvasDocument parent = Boolean.TRUE.equals(parentInfo.getIsCached()) ? redisDocumentReader.read(parentInfo) : getCanvasDocumentOrThrow(parentId);
        // The GitHub service authorizes create_branch before calling this helper.
        if (!CanvasVisibility.allows(parent,user)) throw new CustomException(ErrorCode.ACCESS_DENIED);
        String name = parent.getCanvasName() + " · " + branch;
        var summary = createCanvas(name.substring(0, Math.min(name.length(), 255)), "GitHub branch: " + branch, null, null, user, "private");
        CanvasDocument child = getCanvasDocumentOrThrow(summary.canvasId());
        child.setPeople(new ArrayList<>(parent.getPeople()));
        var groups = new LinkedHashMap<String, List<Long>>();
        parent.getInnerGroup().forEach((key, members) -> groups.put(key, new ArrayList<>(members)));
        child.setInnerGroup(groups);
        child.setGroupBits(new LinkedHashMap<>(parent.getGroupBits()));
        child.setGroupPermissions(new LinkedHashMap<>(parent.getGroupPermissions()));
        child.setInitGroup(parent.getInitGroup());
        child.setAdminUserId(parent.getAdminUserId());
        child.setVisibility(parent.getVisibility());
        child.setCanvasPasswordHash(parent.getCanvasPasswordHash());
        child.setPasswordRevision(parent.getPasswordRevision());
        if (!canvasElasticsearchService.saveCanvas(child)) throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "브랜치 캔버스를 저장하지 못했습니다.");
        return summary;
    }

    @Transactional(readOnly = true)
    public void verifyBranchSourcePassword(Integer sourceId, CustomUserDetails user, String grant) {
        CanvasInfo info=getCanvasInfoWithLockOrThrow(sourceId);
        CanvasDocument source=Boolean.TRUE.equals(info.getIsCached())?redisDocumentReader.read(info):getCanvasDocumentOrThrow(sourceId);
        if(!CanvasVisibility.allows(source,user))throw new CustomException(ErrorCode.ACCESS_DENIED);
        if("public".equals(source.getVisibility())&&source.getCanvasPasswordHash()!=null&&!source.getCanvasPasswordHash().isBlank()
                &&!jwtTokenProvider.validateCanvasPasswordGrant(grant,user.getUserId(),sourceId,source.getPasswordRevision()))
            throw new CustomException(ErrorCode.CANVAS_PASSWORD_REQUIRED);
    }

    @Transactional(readOnly = true)
    public String inheritedBranchPasswordGrant(Integer sourceId,Integer targetId,CustomUserDetails user,String grant) {
        verifyBranchSourcePassword(sourceId,user,grant);
        CanvasInfo sourceInfo=getCanvasInfoWithLockOrThrow(sourceId),targetInfo=getCanvasInfoWithLockOrThrow(targetId);
        CanvasDocument source=Boolean.TRUE.equals(sourceInfo.getIsCached())?redisDocumentReader.read(sourceInfo):getCanvasDocumentOrThrow(sourceId);
        CanvasDocument target=Boolean.TRUE.equals(targetInfo.getIsCached())?redisDocumentReader.read(targetInfo):getCanvasDocumentOrThrow(targetId);
        if(!CanvasVisibility.allows(target,user))throw new CustomException(ErrorCode.ACCESS_DENIED);
        if(!"public".equals(target.getVisibility())||target.getCanvasPasswordHash()==null||target.getCanvasPasswordHash().isBlank())return null;
        if(!java.util.Objects.equals(source.getCanvasPasswordHash(),target.getCanvasPasswordHash()))return null;
        return jwtTokenProvider.createInheritedCanvasPasswordGrant(grant,user.getUserId(),targetId,target.getPasswordRevision());
    }

    /**
     * 2. 캔버스 검색:
     * - 로그인한 사용자만 허용
     * - 캔버스 이름을 기반으로 검색되며 엘라스틱서치로부터 정보를 가져옴
     * - 캔버스 아이디, 대표 이미지, 설명 텍스트, 캔버스 이름, 이용자 수만 제공
     * - MS SQL의 캔버스 정보 테이블은 그 어떤 정보도 제공하지 않음
     */
    public List<CanvasSummaryResponse> searchCanvases(String canvasName, CustomUserDetails currentUser) {
        return searchSpaces(DocumentScope.CANVAS, canvasName, currentUser);
    }

    public List<CanvasSummaryResponse> searchSpaces(DocumentScope scope, String queryName, CustomUserDetails currentUser) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        List<CanvasDocument> docs = scope == DocumentScope.CANVAS
                ? canvasElasticsearchService.searchCanvasesByName(queryName)
                : canvasElasticsearchService.searchDocumentsByName(scope, queryName);
        return docs.stream()
                .filter(doc -> CanvasVisibility.allows(doc, currentUser))
                .filter(doc -> {
                    try {
                        var info = findSpaceById(scope, doc.getCanvasId());
                        return info.isPresent() && (!Boolean.TRUE.equals(info.get().getIsCached())
                                || CanvasVisibility.allows(readCachedDocument(scope, info.get(), null), currentUser));
                    } catch (RuntimeException unavailable) { return false; }
                })
                .map(doc -> CanvasSummaryResponse.of(doc, scope.representativeImagePath(doc.getCanvasId())))
                .toList();
    }

    @Transactional(readOnly = true)
    public CanvasSummaryResponse getCanvasSummary(Integer canvasId, CustomUserDetails currentUser) {
        return getSummary(DocumentScope.CANVAS, canvasId, currentUser);
    }

    @Transactional(readOnly = true)
    public CanvasSummaryResponse getSummary(DocumentScope scope, Integer id, CustomUserDetails currentUser) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        SpaceInfo info = getSpaceInfoWithLockOrThrow(scope, id);
        CanvasDocument doc = Boolean.TRUE.equals(info.getIsCached()) ? readCachedDocument(scope, info, null) : getDocumentOrThrow(scope, id);
        if (!CanvasVisibility.allows(doc, currentUser)) throw new CustomException(scope.notFoundError());

        return CanvasSummaryResponse.of(doc, scope.representativeImagePath(id));
    }

    @Transactional(readOnly = true)
    public CanvasDocument getCanvasDocument(Integer canvasId, CustomUserDetails currentUser) {
        return getCanvasItemDocument(canvasId, currentUser, null);
    }

    @Transactional(readOnly = true)
    public CanvasDocument getCanvasItemDocument(Integer canvasId, CustomUserDetails currentUser, String itemId) {
        return getItemDocument(DocumentScope.CANVAS, canvasId, currentUser, itemId);
    }

    @Transactional(readOnly = true)
    public CanvasDocument getItemDocument(DocumentScope scope, Integer id, CustomUserDetails currentUser, String itemId) {
        if (currentUser == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        SpaceInfo info = getSpaceInfoWithLockOrThrow(scope, id);
        CanvasDocument doc = Boolean.TRUE.equals(info.getIsCached()) ? readCachedDocument(scope, info, itemId) : getDocumentOrThrow(scope, id);
        if (!CanvasVisibility.allows(doc, currentUser)) throw new CustomException(scope.notFoundError());
        return doc;
    }

    @Transactional
    public CanvasUpdateDtos.SettingsResponse getCanvasSettings(Integer canvasId, CustomUserDetails currentUser) {
        return getSettings(DocumentScope.CANVAS, canvasId, currentUser);
    }

    @Transactional
    public CanvasUpdateDtos.SettingsResponse getSettings(DocumentScope scope, Integer id, CustomUserDetails currentUser) {
        SpaceInfo info = getSpaceInfoWithLockOrThrow(scope, id);
        boolean cached = Boolean.TRUE.equals(info.getIsCached());
        CanvasDocument doc = cached ? readCachedDocument(scope, info, null) : getDocumentOrThrow(scope, id);
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED);
        }
        if (!CanvasVisibility.allows(doc, currentUser)) {
            throw new CustomException(ErrorCode.ACCESS_DENIED);
        }
        if (!cached) doc = migrateLegacyPassword(scope, doc);
        List<Long> people = doc.getPeople() == null ? List.of() : doc.getPeople();
        Map<Long, CanvasUpdateDtos.ParticipantResponse> users = new LinkedHashMap<>();
        userRepository.findAllById(people).forEach(user -> users.put(user.getUserId(),
                new CanvasUpdateDtos.ParticipantResponse(user.getNickname(), user.getTagNumber())));
        List<CanvasUpdateDtos.ParticipantResponse> participants = people.stream()
                .map(users::get).filter(java.util.Objects::nonNull).toList();
        Map<String,List<CanvasUpdateDtos.ParticipantResponse>> groups = new LinkedHashMap<>();
        if(doc.getInnerGroup()!=null)doc.getInnerGroup().forEach((key,members)->groups.put(key,members.stream().map(users::get).filter(java.util.Objects::nonNull).toList()));
        return new CanvasUpdateDtos.SettingsResponse(id, doc.getCanvasName(), doc.getDescription(),
                "public".equals(doc.getVisibility()) && doc.getCanvasPasswordHash() != null && !doc.getCanvasPasswordHash().isBlank(),
                doc.getSettingsRevision(), participants, doc.getVisibility(),CanvasGroupPermissions.policies(doc),CanvasGroupPermissions.admin(doc,currentUser.getUserId()),groups,doc.getInitGroup(),users.get(doc.getAdminUserId()));
    }

    @Transactional
    public String updateRepresentativeImage(Integer canvasId, MultipartFile image, CustomUserDetails currentUser) {
        return updateRepresentativeImage(DocumentScope.CANVAS, canvasId, image, currentUser);
    }

    @Transactional
    public String updateRepresentativeImage(DocumentScope scope, Integer id, MultipartFile image, CustomUserDetails currentUser) {
        SpaceInfo info = getSpaceInfoWithLockOrThrow(scope, id);
        CanvasDocument doc = Boolean.TRUE.equals(info.getIsCached())
                ? readCachedDocument(scope, info, null) : getDocumentOrThrow(scope, id);
        validateCanvasAdminGroupOrSystemAdmin(doc, currentUser);
        if (image == null || image.isEmpty()) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "이미지 파일을 선택하세요.");
        }
        return scope == DocumentScope.CANVAS
                ? canvasResourceService.saveRepresentativeImage(id, image)
                : canvasResourceService.saveRepresentativeImage(scope, id, image);
    }

    @Transactional
    public void updateCanvasName(Integer canvasId, String name, CustomUserDetails currentUser) {
        updateName(DocumentScope.CANVAS, canvasId, name, currentUser);
    }

    @Transactional
    public void updateName(DocumentScope scope, Integer id, String name, CustomUserDetails currentUser) {
        CanvasDocument doc = editableInactiveSpace(scope, id, currentUser);
        if (name == null || name.isBlank()) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "이름을 입력하세요.");
        patchFields(scope, id, withNextRevision(doc, Map.of("canvas-name", name.trim())));
    }

    @Transactional
    public void updateCanvasDescription(Integer canvasId, String description, CustomUserDetails currentUser) {
        updateDescription(DocumentScope.CANVAS, canvasId, description, currentUser);
    }

    @Transactional
    public void updateDescription(DocumentScope scope, Integer id, String description, CustomUserDetails currentUser) {
        CanvasDocument doc = editableInactiveSpace(scope, id, currentUser);
        if (description == null) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "설명을 입력하세요.");
        patchFields(scope, id, withNextRevision(doc, Map.of("description", description)));
    }

    @Transactional
    public void updateCanvasPassword(Integer canvasId, String password, CustomUserDetails currentUser) {
        updatePassword(DocumentScope.CANVAS, canvasId, password, currentUser);
    }

    @Transactional
    public void updatePassword(DocumentScope scope, Integer id, String password, CustomUserDetails currentUser) {
        CanvasDocument doc = editableInactiveSpace(scope, id, currentUser);
        if (password == null) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "비밀번호를 입력하세요.");
        if (!"public".equals(doc.getVisibility()) && !password.isBlank()) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "비공개 공간에는 비밀번호를 설정할 수 없습니다.");
        if (password.getBytes(StandardCharsets.UTF_8).length > 72) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "비밀번호는 UTF-8 기준 72바이트 이하여야 합니다.");
        }
        Map<String, Object> fields = new LinkedHashMap<>();
        fields.put("canvas-password-hash", password.isBlank() ? null : CanvasPasswords.hashForStorage(password));
        fields.put("password-revision", doc.getPasswordRevision() + 1L);
        patchFields(scope, id, withNextRevision(doc, fields));
    }

    @Transactional
    public void addCanvasParticipant(Integer canvasId, String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        addParticipant(DocumentScope.CANVAS, canvasId, nickname, tagNumber, currentUser);
    }

    @Transactional
    public void addParticipant(DocumentScope scope, Integer id, String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        CanvasDocument doc = editableInactiveSpace(scope, id, currentUser);
        User target = findParticipant(nickname, tagNumber, true);
        if (doc.getPeople().contains(target.getUserId())) {
            throw new CustomException(ErrorCode.BAD_REQUEST, "이미 참여 중인 사용자입니다.");
        }
        List<Long> people = new ArrayList<>(doc.getPeople());
        people.add(target.getUserId());
        Map<String, List<Long>> groups = copyGroups(doc.getInnerGroup());
        String initialGroup = doc.getInitGroup() == null || doc.getInitGroup().isBlank() ? "default" : doc.getInitGroup();
        groups.computeIfAbsent(initialGroup, unused -> new ArrayList<>()).add(target.getUserId());
        patchFields(scope, id, withNextRevision(doc, Map.of("people", people, "inner-group", groups)));
    }

    @Transactional
    public void removeCanvasParticipant(Integer canvasId, String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        removeParticipant(DocumentScope.CANVAS, canvasId, nickname, tagNumber, currentUser);
    }

    @Transactional
    public void removeParticipant(DocumentScope scope, Integer id, String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        CanvasDocument doc = editableInactiveSpace(scope, id, currentUser);
        User target = findParticipant(nickname, tagNumber, false);
        if (target.getUserId().equals(doc.getAdminUserId())) {
            throw new CustomException(ErrorCode.BAD_REQUEST, "소유자는 제외할 수 없습니다.");
        }
        if (!doc.getPeople().contains(target.getUserId())) {
            throw new CustomException(ErrorCode.BAD_REQUEST, "참여하지 않은 사용자입니다.");
        }
        List<Long> people = new ArrayList<>(doc.getPeople());
        people.remove(target.getUserId());
        Map<String, List<Long>> groups = copyGroups(doc.getInnerGroup());
        groups.values().forEach(members -> members.remove(target.getUserId()));
        patchFields(scope, id, withNextRevision(doc, Map.of("people", people, "inner-group", groups)));
    }

    private CanvasDocument editableInactiveSpace(DocumentScope scope, Integer id, CustomUserDetails currentUser) {
        if (Boolean.TRUE.equals(getSpaceInfoWithLockOrThrow(scope, id).getIsCached())) {
            throw new CustomException(scope.activeError());
        }
        CanvasDocument doc = getDocumentOrThrow(scope, id);
        validateCanvasAdminGroupOrSystemAdmin(doc, currentUser);
        return migrateLegacyPassword(scope, doc);
    }

    private User findParticipant(String nickname, Integer tagNumber, boolean requireActive) {
        if (nickname == null || nickname.isBlank() || tagNumber == null || tagNumber < 1) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "닉네임과 태그 번호를 확인하세요.");
        }
        User user = userRepository.findByNicknameAndTagNumber(nickname.trim(), tagNumber)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));
        if (requireActive && user.getStatus() != UserStatus.ACTIVE) {
            throw new CustomException(ErrorCode.BAD_REQUEST, "활성 사용자만 참여자로 추가할 수 있습니다.");
        }
        return user;
    }

    private Map<String, List<Long>> copyGroups(Map<String, List<Long>> source) {
        Map<String, List<Long>> copy = new LinkedHashMap<>();
        if (source != null) source.forEach((name, members) -> copy.put(name, new ArrayList<>(members)));
        return copy;
    }

    private Map<String, Object> withNextRevision(CanvasDocument doc, Map<String, Object> fields) {
        Map<String, Object> updated = new LinkedHashMap<>(fields);
        updated.put("settings-revision", (doc.getSettingsRevision() == null ? 0L : doc.getSettingsRevision()) + 1L);
        return updated;
    }

    // =========================================================================
    // 4. 캔버스 변경 API (어드민 그룹 또는 시스템 관리자 전용)
    // =========================================================================



    /**
     * 4.5 캔버스 삭제:
     * - 캔버스 소유 사용자(admin-user-id) 또는 관리자만 가능
     * - 활성 캐시 또는 사용자 세션이 있으면 삭제를 거부한다. 비활성 캔버스의 SQL·Elasticsearch·리소스를 삭제한다.
     * - ~/project-agora/canvas-resource/{canvas-id} 디렉터리 정리
     */
    @Transactional
    public void deleteCanvas(Integer canvasId, CustomUserDetails currentUser) {
        delete(DocumentScope.CANVAS, canvasId, currentUser);
    }

    @Transactional
    public void delete(DocumentScope scope, Integer id, CustomUserDetails currentUser) {
        SpaceInfo info = getSpaceInfoWithLockOrThrow(scope, id);
        CanvasDocument doc = getDocumentOrThrow(scope, id);
        validateCanvasOwnerOrSystemAdmin(doc, currentUser);

        if (Boolean.TRUE.equals(info.getIsCached())) {
            log.warn("{} #{} 삭제 실패: 활성화(캐시) 상태에서는 삭제할 수 없습니다.", scope.wireName(), id);
            throw new CustomException(scope.activeError(),
                    "현재 활성화 상태이므로 삭제할 수 없습니다. 모든 사용자가 연결을 종료한 후 다시 시도해 주세요.");
        }

        // Persist cleanup in the same SQL transaction; rollback must preserve VM files.
        if (scope == DocumentScope.CANVAS) {
            runtimeCleanupQueue.enqueue(id);
            canvasInfoRepository.delete((CanvasInfo) info);
            if (!canvasElasticsearchService.deleteCanvas(id, doc.getCanvasName())) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR,
                        "캔버스 저장 문서를 삭제하지 못했습니다. 잠시 후 다시 시도해 주세요.");
            }
            canvasResourceService.deleteCanvasResourceDirectory(id);
            events.publishEvent(new com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupCoordinator.Deleted(id));
        } else {
            runtimeCleanupQueue.enqueue(scope, id);
            if (workspaceInfoRepository != null) {
                workspaceInfoRepository.delete((WorkspaceInfo) info);
            }
            if (!canvasElasticsearchService.deleteDocument(scope, id, doc.getCanvasName())) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR,
                        "워크스페이스 저장 문서를 삭제하지 못했습니다. 잠시 후 다시 시도해 주세요.");
            }
            canvasResourceService.deleteResourceDirectory(scope, id);
            events.publishEvent(new com.endpoint.frelog.domain.canvas.runtime.RuntimeCleanupCoordinator.DeletedSpace(scope, id));
        }
        log.info("{} #{} 삭제 완료; VM 정리는 커밋 후 수행", scope.wireName(), id);
    }

    @Transactional
    public CanvasUpdateDtos.AccessResponse accessCanvas(Integer canvasId, jakarta.servlet.http.HttpServletRequest httpRequest, CustomUserDetails currentUser) {
        return accessCanvas(canvasId, httpRequest, currentUser, null);
    }

    @Transactional
    public CanvasUpdateDtos.AccessResponse accessCanvas(Integer canvasId, jakarta.servlet.http.HttpServletRequest httpRequest,
                                                         CustomUserDetails currentUser, String suppliedPassword) {
        return accessCanvas(canvasId, httpRequest, currentUser, suppliedPassword, null);
    }

    @Transactional
    public CanvasUpdateDtos.AccessResponse accessCanvas(Integer canvasId, jakarta.servlet.http.HttpServletRequest httpRequest,
                                                         CustomUserDetails currentUser, String suppliedPassword,
                                                         String suppliedPasswordToken) {
        return access(DocumentScope.CANVAS, canvasId, httpRequest, currentUser, suppliedPassword, suppliedPasswordToken);
    }

    @Transactional
    public CanvasUpdateDtos.AccessResponse access(DocumentScope scope, Integer id, jakarta.servlet.http.HttpServletRequest httpRequest,
                                                    CustomUserDetails currentUser, String suppliedPassword,
                                                    String suppliedPasswordToken) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        // Lock the allocation row before choosing Elasticsearch or the live Redis document.
        SpaceInfo spaceInfo = getSpaceInfoWithLockOrThrow(scope, id);

        boolean cached = Boolean.TRUE.equals(spaceInfo.getIsCached());
        CanvasDocument doc = cached ? readCachedDocument(scope, spaceInfo, null) : getDocumentOrThrow(scope, id);
        if (!CanvasVisibility.allows(doc, currentUser)) throw new CustomException(scope.notFoundError());

        String storedPassword = doc.getCanvasPasswordHash();
        long settingsRevision = doc.getSettingsRevision() == null ? 0L : doc.getSettingsRevision();
        boolean passwordProtected = "public".equals(doc.getVisibility()) && storedPassword != null && !storedPassword.isBlank();
        boolean validPasswordToken = passwordProtected && (scope == DocumentScope.CANVAS
                ? jwtTokenProvider.validateCanvasPasswordGrant(
                        suppliedPasswordToken, currentUser.getUserId(), id, doc.getPasswordRevision())
                : jwtTokenProvider.validatePasswordGrant(
                        scope, suppliedPasswordToken, currentUser.getUserId(), id, doc.getPasswordRevision()));
        if (passwordProtected && !validPasswordToken) {
            if (suppliedPassword == null || suppliedPassword.isBlank()) {
                throw new CustomException(scope.passwordRequiredError());
            }
            boolean matches = CanvasPasswords.matches(suppliedPassword, storedPassword);
            if (!matches) throw new CustomException(scope.passwordInvalidError());
        }

        String serverIp = "none";
        String wsPort = "none";
        Integer serverId = null;

        ServerInfo assignedServer = spaceInfo.getCppServer();
        boolean assignedServerAvailable = Boolean.TRUE.equals(spaceInfo.getIsCached())
                && assignedServer != null
                && Boolean.TRUE.equals(assignedServer.getIsActivated())
                && assignedServer.getLastHeartbeatAt() != null
                && assignedServer.getLastHeartbeatAt().isAfter(LocalDateTime.now(ZoneOffset.UTC).minusSeconds(15));

        if (!assignedServerAvailable) {
            if (assignedServer != null) {
                log.warn("{} #{}의 기존 C++ 서버 #{} heartbeat가 만료되었거나 비활성 상태라 할당을 해제합니다.",
                        scope.wireName(), id, assignedServer.getServerId());
                spaceInfo.setCppServer(null);
                saveSpaceInfo(scope, spaceInfo);
            }
            AllocateServerResponse serverAlloc = loadBalancerService.allocateServer();
            ServerInfo sInfo = serverInfoRepository.findByServerIpAndServerPort(serverAlloc.serverIp(), serverAlloc.serverPort()).orElse(null);

            if (sInfo != null) {
                serverIp = sInfo.getServerIp();
                wsPort = sInfo.getWsPort();
                serverId = sInfo.getServerId();
            }
            log.info("{} #{} 신규 접속 요청: 클라이언트에게 접속할 C++ 서버 안내 완료 [Server={}:{}]",
                    scope.wireName(), id, serverIp, wsPort);
        } else {
            serverIp = assignedServer.getServerIp();
            wsPort = assignedServer.getWsPort();
            serverId = assignedServer.getServerId();
        }

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

        String canvasPasswordToken = passwordProtected
                ? validPasswordToken
                    ? suppliedPasswordToken
                    : (scope == DocumentScope.CANVAS
                        ? jwtTokenProvider.createCanvasPasswordGrant(currentUser.getUserId(), id, doc.getPasswordRevision())
                        : jwtTokenProvider.createPasswordGrant(scope, currentUser.getUserId(), id, doc.getPasswordRevision()))
                : null;
        boolean bot = scope == DocumentScope.CANVAS
                && com.endpoint.frelog.domain.canvas.bot.CanvasBotService.isBot(currentUser.getUser());
        long lifetimeMs = passwordProtected
                ? (scope == DocumentScope.CANVAS
                    ? jwtTokenProvider.getCanvasPasswordGrantRemainingMs(canvasPasswordToken)
                    : jwtTokenProvider.getPasswordGrantRemainingMs(scope, canvasPasswordToken))
                : 0L;
        if (bot) {
            lifetimeMs = lifetimeMs > 0
                    ? Math.min(lifetimeMs, 60_000L)
                    : 60_000L;
        }
        String nickname = bot ? currentUser.getNickname() : currentUser.getUser().getNickname();
        String accessToken;
        if (scope == DocumentScope.CANVAS) {
            accessToken = lifetimeMs > 0
                    ? jwtTokenProvider.createCanvasAccessToken(
                        nickname, currentUser.getUser().getTagNumber(), id,
                        httpRequest.getRemoteAddr(), serverHash, settingsRevision, lifetimeMs)
                    : jwtTokenProvider.createCanvasAccessToken(
                        nickname, currentUser.getUser().getTagNumber(), id,
                        httpRequest.getRemoteAddr(), serverHash, settingsRevision);
        } else {
            accessToken = lifetimeMs > 0
                    ? jwtTokenProvider.createAccessToken(
                        scope, nickname, currentUser.getUser().getTagNumber(), id,
                        httpRequest.getRemoteAddr(), serverHash, settingsRevision, lifetimeMs)
                    : jwtTokenProvider.createWorkspaceAccessToken(
                        nickname, currentUser.getUser().getTagNumber(), id,
                        httpRequest.getRemoteAddr(), serverHash, settingsRevision);
        }

        return new CanvasUpdateDtos.AccessResponse(serverId, wsPort, accessToken, canvasPasswordToken);
    }

    private SpaceInfo createSpaceInfo(DocumentScope scope) {
        if (scope == DocumentScope.WORKSPACE) {
            if (workspaceInfoRepository == null) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "워크스페이스 저장소가 초기화되지 않았습니다.");
            }
            return workspaceInfoRepository.save(new WorkspaceInfo());
        }
        return canvasInfoRepository.save(new CanvasInfo());
    }

    private Optional<? extends SpaceInfo> findSpaceById(DocumentScope scope, Integer id) {
        if (scope == DocumentScope.WORKSPACE) {
            return workspaceInfoRepository == null ? Optional.empty() : workspaceInfoRepository.findById(id);
        }
        return canvasInfoRepository.findById(id);
    }

    private void saveSpaceInfo(DocumentScope scope, SpaceInfo info) {
        if (scope == DocumentScope.WORKSPACE) {
            workspaceInfoRepository.save((WorkspaceInfo) info);
        } else {
            canvasInfoRepository.save((CanvasInfo) info);
        }
    }

    private CanvasDocument readCachedDocument(DocumentScope scope, SpaceInfo info, String itemId) {
        if (scope == DocumentScope.CANVAS) {
            CanvasInfo canvasInfo = (CanvasInfo) info;
            return itemId == null ? redisDocumentReader.read(canvasInfo) : redisDocumentReader.read(canvasInfo, itemId);
        }
        return itemId == null ? redisDocumentReader.read(scope, info) : redisDocumentReader.read(scope, info, itemId);
    }

    private void patchFields(DocumentScope scope, Integer id, Map<String, Object> fields) {
        if (scope == DocumentScope.CANVAS) {
            canvasElasticsearchService.patchCanvasFields(id, fields);
        } else {
            canvasElasticsearchService.patchFields(scope, id, fields);
        }
    }

    private CanvasDocument getCanvasDocumentOrThrow(Integer canvasId) {
        return getDocumentOrThrow(DocumentScope.CANVAS, canvasId);
    }

    private CanvasDocument getDocumentOrThrow(DocumentScope scope, Integer id) {
        return (scope == DocumentScope.CANVAS
                ? canvasElasticsearchService.getCanvasDocumentById(id)
                : canvasElasticsearchService.getDocumentById(scope, id))
                .orElseThrow(() -> new CustomException(scope.notFoundError(),
                        scope.wireName() + "을(를) 찾을 수 없습니다: " + id));
    }

    private CanvasDocument migrateLegacyCanvasPassword(CanvasDocument doc) {
        return migrateLegacyPassword(DocumentScope.CANVAS, doc);
    }

    private CanvasDocument migrateLegacyPassword(DocumentScope scope, CanvasDocument doc) {
        String password = doc.getCanvasPasswordHash();
        if (password != null && !password.isBlank() && !CanvasPasswords.isHash(password)) {
            doc.setCanvasPasswordHash(CanvasPasswords.hashForStorage(password));
            boolean saved = scope == DocumentScope.CANVAS
                    ? canvasElasticsearchService.saveCanvas(doc)
                    : canvasElasticsearchService.saveDocument(scope, doc);
            if (!saved) {
                throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "기존 비밀번호를 안전하게 이전하지 못했습니다.");
            }
        }
        return doc;
    }

    private CanvasInfo getCanvasInfoWithLockOrThrow(Integer canvasId) {
        return canvasInfoRepository.findByIdWithPessimisticLock(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND, "캔버스 메타데이터를 찾을 수 없습니다: " + canvasId));
    }

    private SpaceInfo getSpaceInfoWithLockOrThrow(DocumentScope scope, Integer id) {
        if (scope == DocumentScope.WORKSPACE) {
            Optional<WorkspaceInfo> found = workspaceInfoRepository == null
                    ? Optional.empty()
                    : workspaceInfoRepository.findByIdWithPessimisticLock(id);
            return found.orElseThrow(() -> new CustomException(ErrorCode.WORKSPACE_NOT_FOUND, "워크스페이스 메타데이터를 찾을 수 없습니다: " + id));
        }
        return getCanvasInfoWithLockOrThrow(id);
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
