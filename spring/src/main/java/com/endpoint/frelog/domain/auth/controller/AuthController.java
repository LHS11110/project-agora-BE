package com.endpoint.frelog.domain.auth.controller;

import com.endpoint.frelog.domain.auth.dto.LoginRequest;
import com.endpoint.frelog.domain.auth.dto.LoginResponse;
import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.auth.service.AuthService;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.security.core.userdetails.UserDetails;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.Map;

@RestController
@RequestMapping("/api/auth")
public class AuthController {

    private final AuthService authService;

    public AuthController(AuthService authService) {
        this.authService = authService;
    }

    @PostMapping("/login")
    public ResponseEntity<LoginResponse> login(@Valid @RequestBody LoginRequest request) {
        LoginResponse response = authService.login(request);
        return ResponseEntity.ok(response);
    }

    @PostMapping("/signup")
    public ResponseEntity<UserResponse> signup(@Valid @RequestBody SignupRequest request) {
        UserResponse response = authService.signup(request);
        return ResponseEntity.status(HttpStatus.CREATED).body(response);
    }

    @GetMapping("/me")
    public ResponseEntity<UserResponse> getMe(@AuthenticationPrincipal UserDetails userDetails) {
        UserResponse response = authService.getMe(userDetails.getUsername());
        return ResponseEntity.ok(response);
    }

    @GetMapping("/users")
    public ResponseEntity<java.util.List<UserResponse>> listUsers() {
        java.util.List<UserResponse> response = authService.listUsers();
        return ResponseEntity.ok(response);
    }

    @GetMapping("/health")
    public ResponseEntity<Map<String, String>> healthCheck() {
        return ResponseEntity.ok(Map.of("status", "UP", "service", "Agora Auth Service"));
    }
}

