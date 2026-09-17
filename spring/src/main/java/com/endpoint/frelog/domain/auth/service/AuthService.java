package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.canvas.client.CppServerClient;
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
import java.time.LocalDateTime;
import java.util.HexFormat;
import java.util.List;

@Service
public class AuthService {

    private static final Logger log = LoggerFactory.getLogger(AuthService.class);

    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final JwtTokenProvider jwtTokenProvider;
    private final CppServerClient cppServerClient;
    private final UserSessionRepository userSessionRepository;

    public AuthService(UserRepository userRepository,
                       PasswordEncoder passwordEncoder,
                       JwtTokenProvider jwtTokenProvider,
                       CppServerClient cppServerClient,
                       UserSessionRepository userSessionRepository) {
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
        this.jwtTokenProvider = jwtTokenProvider;
        this.cppServerClient = cppServerClient;
        this.userSessionRepository = userSessionRepository;
    }

    @Transactional
    public LoginResponse login(LoginRequest request) {
        User user = userRepository.findByEmail(request.email())
                .orElseThrow(() -> new CustomException(ErrorCode.INVALID_CREDENTIALS));

        if (user.getStatus() == UserStatus.SUSPENDED) {
            throw new CustomException(ErrorCode.USER_SUSPENDED);
        }

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            throw new CustomException(ErrorCode.USER_WITHDRAWN);
        }

        if (user.getPasswordHash() == null || !passwordEncoder.matches(request.password(), user.getPasswordHash())) {
            throw new CustomException(ErrorCode.INVALID_CREDENTIALS);
        }

        UserSession session = userSessionRepository.findById(user.getUserId()).orElseGet(() -> new UserSession(user));
        session.updateLastLogin(LocalDateTime.now());
        userSessionRepository.save(session);

        // Multi-session login: stateless JWT issued per login request
        String token = jwtTokenProvider.createToken(
                user.getEmail(),
                user.getUserId(),
                user.getNickname(),
                user.getRole().name()
        );

        return LoginResponse.of(token, UserResponse.from(user));
    }

    @Transactional
    public UserResponse signup(SignupRequest request) {
        if (userRepository.existsByEmail(request.email())) {
            throw new CustomException(ErrorCode.EMAIL_ALREADY_EXISTS);
        }

        String encodedPassword = passwordEncoder.encode(request.password());
        Integer nextTagNumber = userRepository.findMaxTagNumberByNickname(request.nickname()) + 1;
        User newUser = new User(request.email(), encodedPassword, request.nickname(), nextTagNumber, Role.ROLE_USER);
        User savedUser = userRepository.save(newUser);
        
        UserSession newSession = new UserSession(savedUser);
        userSessionRepository.save(newSession);

        return UserResponse.from(savedUser);
    }

    @Transactional(readOnly = true)
    public UserResponse getMe(String email) {
        User user = userRepository.findByEmail(email)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));

        return UserResponse.from(user);
    }

    /**
     * 사용자 조회 (해당 사용자 본인 또는 ROLE_ADMIN 만 허용)
     */
    @Transactional(readOnly = true)
    public UserResponse getUserById(Long userId, CustomUserDetails currentUser) {
        validateSelfOrAdmin(userId, currentUser);

        User user = userRepository.findById(userId)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다: " + userId));
        return UserResponse.from(user);
    }

    /**
     * 사용자 정보 변경 (해당 사용자 본인 또는 ROLE_ADMIN 만 허용)
     */
    @Transactional
    public UserResponse updateUser(Long userId, UpdateUserRequest request, CustomUserDetails currentUser) {
        validateSelfOrAdmin(userId, currentUser);

        User user = userRepository.findById(userId)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다: " + userId));

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            throw new CustomException(ErrorCode.USER_WITHDRAWN, "탈퇴한 회원은 수정할 수 없습니다.");
        }

        if (request.nickname() != null && !request.nickname().isBlank()) {
            String newNickname = request.nickname().trim();
            if (!newNickname.equals(user.getNickname())) {
                user.setNickname(newNickname);
                user.setTagNumber(userRepository.findMaxTagNumberByNickname(newNickname) + 1);
            }
        }

        if (request.password() != null && !request.password().isBlank()) {
            user.changePassword(passwordEncoder.encode(request.password()));
        }

        User saved = userRepository.save(user);
        return UserResponse.from(saved);
    }

    /**
     * 사용자 삭제:
     * - 해당 사용자 본인 또는 ROLE_ADMIN 만 허용
     * - user 테이블을 통해 접속 중(is_accessed == true)인지 확인:
     *   접속 중이면 C++ 서버의 접속을 중단하고 삭제
     *   접속 중이지 않으면 즉시 수행
     * - 모든 삭제는 유저명을 `deleted user-[hash value]`로 바꾸고 상태를 `WITHDRAWN`으로 변경만 하고 실제 데이터는 유지 (소프트 딜리트)
     */
    @Transactional
    public void deleteUser(Long userId, CustomUserDetails currentUser) {
        validateSelfOrAdmin(userId, currentUser);

        User user = userRepository.findById(userId)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다: " + userId));

        if (user.getStatus() == UserStatus.WITHDRAWN) {
            return; // 이미 탈퇴된 계정
        }

        // 1. 접속 중인지 확인 후 C++ 서버에 접속 종료 요청
        UserSession session = userSessionRepository.findById(userId).orElse(null);
        if (session != null && Boolean.TRUE.equals(session.getIsAccessed())) {
            String serverIp = session.getCppServer() != null ? session.getCppServer().getServerIp() : null;
            String serverPort = session.getCppServer() != null ? session.getCppServer().getServerPort() : null;
            log.info("회원 #{}가 실시간 서버({}:{})에 접속 중이므로 C++ 서버 접속 중단 요청을 전송합니다.",
                    userId, serverIp, serverPort);
            if (serverIp != null && serverPort != null) {
                cppServerClient.disconnectUser(serverIp, serverPort, userId);
            }
        }

        // 2. 해시 생성 및 유저명 변경, 상태 WITHDRAWN 변경
        String hashValue = generateHash(userId);
        user.setNickname("deleted user-" + hashValue);
        user.setStatus(UserStatus.WITHDRAWN);

        userRepository.save(user);

        if (session != null) {
            session.setIsAccessed(false);
            session.setCppServer(null);
            userSessionRepository.save(session);
        }

        log.info("회원 #{} 소프트 딜리트 완료: nickname='{}', status=WITHDRAWN", userId, user.getNickname());
    }

    private String generateHash(Long userId) {
        try {
            String raw = userId + "-" + System.currentTimeMillis();
            MessageDigest digest = MessageDigest.getInstance("SHA-256");
            byte[] hash = digest.digest(raw.getBytes(java.nio.charset.StandardCharsets.UTF_8));
            return HexFormat.of().formatHex(hash).substring(0, 16);
        } catch (NoSuchAlgorithmException e) {
            return Long.toHexString(System.currentTimeMillis());
        }
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
