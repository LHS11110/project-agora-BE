package com.endpoint.frelog.domain.auth.service;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.JwtTokenProvider;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.time.LocalDateTime;

@Service
public class AuthService {

    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;
    private final JwtTokenProvider jwtTokenProvider;

    public AuthService(UserRepository userRepository,
                       PasswordEncoder passwordEncoder,
                       JwtTokenProvider jwtTokenProvider) {
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
        this.jwtTokenProvider = jwtTokenProvider;
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

        user.updateLastLogin(LocalDateTime.now());
        userRepository.save(user);

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
        User newUser = new User(request.email(), encodedPassword, request.nickname(), Role.ROLE_USER);
        User savedUser = userRepository.save(newUser);

        return UserResponse.from(savedUser);
    }

    @Transactional(readOnly = true)
    public UserResponse getMe(String email) {
        User user = userRepository.findByEmail(email)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));

        return UserResponse.from(user);
    }

    @Transactional(readOnly = true)
    public UserResponse getUserById(Long userId) {
        User user = userRepository.findById(userId)
                .orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND, "사용자를 찾을 수 없습니다: " + userId));
        return UserResponse.from(user);
    }

    @Transactional(readOnly = true)
    public java.util.List<UserResponse> listUsers() {
        return userRepository.findAll().stream()
                .map(UserResponse::from)
                .toList();
    }
}

