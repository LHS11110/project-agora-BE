package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.user.dto.UpdateUserRequest;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.security.MessageDigest;
import java.security.NoSuchAlgorithmException;
import java.nio.charset.StandardCharsets;
import java.time.LocalDateTime;
import java.util.HexFormat;
import java.util.List;
import jakarta.servlet.http.HttpServletRequest;

@Service
public class AuthService {

    private static final Logger log = LoggerFactory.getLogger(AuthService.class);

    private final com.endpoint.frelog.domain.user.service.UserErasureService erasure;
    private final org.springframework.context.ApplicationEventPublisher events;
    private final com.endpoint.frelog.domain.auth.email.EmailVerificationService emailVerification;
    private final com.endpoint.frelog.domain.auth.mfa.MfaService mfa;
    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final JwtTokenProvider jwtTokenProvider;
    private final UserSessionRepository userSessionRepository;

    public AuthService(UserRepository userRepository,
                       PasswordEncoder passwordEncoder,
                       JwtTokenProvider jwtTokenProvider,
                       UserSessionRepository userSessionRepository, org.springframework.context.ApplicationEventPublisher events, com.endpoint.frelog.domain.user.service.UserErasureService erasure, com.endpoint.frelog.domain.auth.mfa.MfaService mfa, com.endpoint.frelog.domain.auth.email.EmailVerificationService emailVerification) {
        this.events = events;
        this.erasure = erasure;
        this.mfa = mfa;
        this.emailVerification = emailVerification;
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
        this.jwtTokenProvider = jwtTokenProvider;
        this.userSessionRepository = userSessionRepository;
    }

    @Transactional
    public LoginResponse login(LoginRequest request, HttpServletRequest httpRequest) {
        User user = userRepository.findByEmail(request.email())
                .orElseThrow(() -> new CustomException(ErrorCode.INVALID_CREDENTIALS));

        if (com.endpoint.frelog.domain.canvas.bot.CanvasBotService.isBot(user)) throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        if (user.getStatus() == UserStatus.SUSPENDED) {
            throw new CustomException(ErrorCode.USER_SUSPENDED);
        }

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            throw new CustomException(ErrorCode.USER_WITHDRAWN);
        }

        if (request.password().getBytes(StandardCharsets.UTF_8).length > 72
                || user.getPasswordHash() == null
                || !passwordEncoder.matches(request.password(), user.getPasswordHash())) {
            throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        }

        return completeLogin(user, httpRequest);
    }

    /** Called only after the WebAuthn service has verified the assertion and active account. */
    @Transactional
    public LoginResponse completePasskeyLogin(User user, HttpServletRequest request) {
        if (!user.isActive()) throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        return mfa.completeVerifiedPasskey(user, request);
    }

    @Transactional
    public LoginResponse completeOrcidLogin(User user,HttpServletRequest request) {
        if(!user.isActive()||user.getOrcid()==null)throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        if(mfa.enabled(user.getUserId()))return mfa.challenge(user,request);
        UserSession session=userSessionRepository.findByIdWithPessimisticLock(user.getUserId()).orElseGet(()->new UserSession(user));
        session.updateLastLogin(LocalDateTime.now());userSessionRepository.save(session);
        return LoginResponse.of(jwtTokenProvider.createOrcidToken(user,request.getRemoteAddr(),null),UserResponse.from(user));
    }

    private LoginResponse completeLogin(User user, HttpServletRequest httpRequest) {
        if (mfa.enabled(user.getUserId())) return mfa.challenge(user, httpRequest);
        UserSession session = userSessionRepository.findByIdWithPessimisticLock(user.getUserId())
                .orElseGet(() -> new UserSession(user));
        session.updateLastLogin(LocalDateTime.now());
        userSessionRepository.save(session);

        // Multi-session login: stateless JWT issued per login request
        String clientIp = httpRequest.getRemoteAddr();
        String token = jwtTokenProvider.createToken(
                user.getEmail(),
                user.getTagNumber(),
                user.getNickname(),
                user.getRole().name(),
                clientIp
        );

        return LoginResponse.of(token, UserResponse.from(user));
    }

    /** Accept only a principal already verified by Spring Security's OIDC client. */
    @Transactional
    public LoginResponse loginGoogle(org.springframework.security.oauth2.core.oidc.user.OidcUser principal, HttpServletRequest request) {
        if (!Boolean.TRUE.equals(principal.getEmailVerified())) throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        return loginSocial(new com.endpoint.frelog.domain.auth.oauth.SocialIdentity("google",principal.getSubject(),principal.getEmail(),principal.getFullName(),true,principal.getPicture()),request);
    }

    @Transactional
    public LoginResponse loginSocial(com.endpoint.frelog.domain.auth.oauth.SocialIdentity identity,HttpServletRequest request) {
        identity.validate();
        String provider=identity.provider(),subject=identity.subject();
        User user=userRepository.findByOauthProviderAndOauthId(provider,subject).orElse(null);
        // OAuth subjects are opaque and case-sensitive, even on a case-insensitive SQL database.
        if(user!=null&&(!provider.equals(user.getOauthProvider())||!subject.equals(user.getOauthId())))throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        if(user==null||user.getStatus()==UserStatus.WITHDRAWN){
            // An unverified provider email can still identify a signup conflict;
            // it must never authenticate or link the existing account.
            if(identity.email()!=null&&identity.email().length()<=255&&!identity.email().isBlank()){
                var existing=userRepository.findByEmail(com.endpoint.frelog.domain.auth.email.EmailVerificationService.normalize(identity.email())).orElse(null);
                if(existing!=null&&existing.isActive()&&(user==null||!existing.getUserId().equals(user.getUserId())))throw new CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
            }
            if(!identity.hasVerifiedEmail())throw new com.endpoint.frelog.domain.auth.oauth.OAuthEmailRequired();
            if(user!=null){erasure.erase(user);userRepository.saveAndFlush(user);user=null;}
            String email=com.endpoint.frelog.domain.auth.email.EmailVerificationService.normalize(identity.email());
            if(userRepository.existsByEmail(email))throw new CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
            String name=identity.name();
            if(name==null||name.isBlank())name=switch(provider){case "naver"->"네이버 사용자";case "kakao"->"카카오 사용자";default->"Google 사용자";};
            name=name.trim();name=name.substring(0,Math.min(name.length(),100));
            int tag=userRepository.findMaxTagNumberByNicknameForUpdate(name)+1;
            user=new User(email,null,name,tag,Role.ROLE_USER);user.setOauthProvider(provider);user.setOauthId(subject);
            user=userRepository.save(user);
            if(identity.profileImageUrl()!=null&&!identity.profileImageUrl().isBlank())
                events.publishEvent(new com.endpoint.frelog.domain.auth.oauth.SocialProfileImageService.ImportImage(user.getUserId(),identity.profileImageUrl()));
        }
        if(user.getStatus()==UserStatus.SUSPENDED)throw new CustomException(ErrorCode.USER_SUSPENDED);
        if(user.getStatus()==UserStatus.WITHDRAWN)throw new CustomException(ErrorCode.USER_WITHDRAWN);
        if(user.getEmailVerifiedAt()==null&&identity.hasVerifiedEmail()&&user.getEmail().equalsIgnoreCase(identity.email()))user.setEmailVerifiedAt(LocalDateTime.now(java.time.ZoneOffset.UTC));
        return completeLogin(user,request);
    }

    @Transactional
    public LoginResponse completeOAuthEnrollment(com.endpoint.frelog.domain.auth.oauth.SocialIdentity identity,String email,String proof,HttpServletRequest request){
        identity.validate();
        String normalized=com.endpoint.frelog.domain.auth.email.EmailVerificationService.normalize(email);
        if(userRepository.existsByEmail(normalized))throw new CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
        emailVerification.consume(normalized,proof);
        return loginSocial(identity.withVerifiedEmail(normalized),request);
    }

    @Transactional
    public UserResponse signup(SignupRequest request) {
        com.endpoint.frelog.domain.auth.validation.PasswordPolicy.validate(request.password());
        emailVerification.consume(request.email(),request.emailVerificationToken());
        if (userRepository.existsByEmail(request.email())) {
            throw new CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
        }
        String encodedPassword = passwordEncoder.encode(request.password());
        Integer nextTagNumber = userRepository.findMaxTagNumberByNicknameForUpdate(request.nickname()) + 1;
        User newUser = new User(com.endpoint.frelog.domain.auth.email.EmailVerificationService.normalize(request.email()), encodedPassword, request.nickname(), nextTagNumber, Role.ROLE_USER);
        newUser.setEmailVerifiedAt(LocalDateTime.now(java.time.ZoneOffset.UTC));
        User savedUser = userRepository.save(newUser);
        
        UserSession newSession = new UserSession(savedUser);
        userSessionRepository.save(newSession);

        return UserResponse.from(savedUser);
    }

    @Transactional(readOnly = true)
    public UserResponse getMe(String email) {
        User user = (email!=null&&email.matches("irade-user:[0-9]+")?userRepository.findById(Long.parseLong(email.substring(11))):userRepository.findByEmail(email))
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));
        if(!user.isActive()||(email!=null&&email.startsWith("irade-user:")&&user.getOrcid()==null))throw new CustomException(ErrorCode.INVALID_TOKEN);

        return UserResponse.from(user);
    }

    /**
     * 사용자 조회 (해당 사용자 본인 또는 ROLE_ADMIN 만 허용)
     */
    @Transactional(readOnly = true)
    public UserResponse getUserByNicknameAndTagNumber(String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        User user = userRepository.findByNicknameAndTagNumber(nickname, tagNumber)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다."));
        
        validateSelfOrAdmin(user.getUserId(), currentUser);
        return UserResponse.from(user);
    }

    /**
     * 사용자 정보 변경 (해당 사용자 본인 또는 ROLE_ADMIN 만 허용)
     */
    @Transactional
    public UserResponse updateUser(String nickname, Integer tagNumber, UpdateUserRequest request, CustomUserDetails currentUser) {
        User user = userRepository.findByNicknameAndTagNumber(nickname, tagNumber)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다."));

        validateSelfOrAdmin(user.getUserId(), currentUser);

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            throw new CustomException(ErrorCode.USER_WITHDRAWN, "탈퇴한 회원은 수정할 수 없습니다.");
        }

        if (request.nickname() != null && !request.nickname().isBlank()) {
            String newNickname = request.nickname().trim();
            if (!newNickname.equals(user.getNickname())) {
                user.setNickname(newNickname);
                user.setTagNumber(userRepository.findMaxTagNumberByNicknameForUpdate(newNickname) + 1);
            }
        }

        if (request.password() != null) {
            com.endpoint.frelog.domain.auth.validation.PasswordPolicy.validate(request.password());
            user.changePassword(passwordEncoder.encode(request.password()));
        }

        User saved = userRepository.save(user);
        return UserResponse.from(saved);
    }

    /**
     * 사용자 삭제:
     * - 해당 사용자 본인 또는 ROLE_ADMIN 만 허용
     * - 캔버스 이용 중(is_accessed == true)이면 탈퇴 요청을 거부
     * - 접속 중이지 않으면 소프트 딜리트 수행
     * - 개인정보와 인증 정보를 삭제하고 사용자 ID 및 삭제된 계정 표시만 유지
     */
    @Transactional
    public void deleteUser(String nickname, Integer tagNumber, CustomUserDetails currentUser) {
        User user = userRepository.findByNicknameAndTagNumber(nickname, tagNumber)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다."));

        Long userId = user.getUserId();
        validateSelfOrAdmin(userId, currentUser);

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            erasure.erase(user);userRepository.save(user);return;
        }

        // A Spring API must not force-close a live C++ WebSocket. Ask the user
        // to close their own connection, then retry the account deletion.
        UserSession session = userSessionRepository.findByIdWithPessimisticLock(userId).orElse(null);
        if (session != null && Boolean.TRUE.equals(session.getIsAccessed())) {
            throw new CustomException(ErrorCode.USER_ACTIVE,
                    "캔버스를 이용 중인 회원은 탈퇴할 수 없습니다. 캔버스 연결을 종료한 뒤 다시 시도해 주세요.");
        }

        user.setStatus(UserStatus.WITHDRAWN);
        erasure.erase(user);
        userRepository.save(user);
        log.info("회원 #{} 개인정보 삭제 완료: status=WITHDRAWN",userId);
    }

    private void validateSelfOrAdmin(Long targetUserId, CustomUserDetails currentUser) {
        if (currentUser == null || currentUser.getUserId() == null) {
            throw new CustomException(ErrorCode.UNAUTHORIZED, "로그인이 필요한 요청입니다.");
        }

        boolean isSelf = currentUser.getUserId().equals(targetUserId);
        boolean isAdmin = currentUser.getUser() != null && currentUser.getUser().getRole() == Role.ROLE_ADMIN
                || currentUser.getAuthorities().stream().anyMatch(a -> a.getAuthority().equals("ROLE_ADMIN"));

        if (!isSelf && !isAdmin) {
            throw new CustomException(ErrorCode.ACCESS_DENIED, "해당 사용자에 대한 조회/수정/삭제 권한이 없습니다.");
        }
    }

    @Transactional(readOnly = true)
    public List<UserResponse> listUsers() {
        return userRepository.findAll().stream()
                .map(UserResponse::from)
                .toList();
    }
}
