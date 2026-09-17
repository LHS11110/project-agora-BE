package com.endpoint.frelog.domain.user.controller;

import com.endpoint.frelog.domain.auth.dto.SignupRequest;
import com.endpoint.frelog.domain.auth.dto.UserResponse;
import com.endpoint.frelog.domain.auth.service.AuthService;
import com.endpoint.frelog.domain.user.dto.UpdateUserRequest;
import com.endpoint.frelog.global.security.CustomUserDetails;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PatchMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController
@RequestMapping("/api/users")
public class UserController {

    private final AuthService authService;

    public UserController(AuthService authService) {
        this.authService = authService;
    }

    @PostMapping
    public ResponseEntity<UserResponse> createUser(@Valid @RequestBody SignupRequest request) {
        UserResponse response = authService.signup(request);
        return ResponseEntity.status(HttpStatus.CREATED).body(response);
    }

    @GetMapping("/{nickname}/{tagNumber}")
    public ResponseEntity<UserResponse> getUserByNicknameAndTagNumber(
            @PathVariable String nickname,
            @PathVariable Integer tagNumber,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(authService.getUserByNicknameAndTagNumber(nickname, tagNumber, currentUser));
    }

    @PutMapping("/{nickname}/{tagNumber}")
    public ResponseEntity<UserResponse> updateUserPut(
            @PathVariable String nickname,
            @PathVariable Integer tagNumber,
            @Valid @RequestBody UpdateUserRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(authService.updateUser(nickname, tagNumber, request, currentUser));
    }

    @PatchMapping("/{nickname}/{tagNumber}")
    public ResponseEntity<UserResponse> updateUserPatch(
            @PathVariable String nickname,
            @PathVariable Integer tagNumber,
            @Valid @RequestBody UpdateUserRequest request,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        return ResponseEntity.ok(authService.updateUser(nickname, tagNumber, request, currentUser));
    }

    @DeleteMapping("/{nickname}/{tagNumber}")
    public ResponseEntity<Void> deleteUser(
            @PathVariable String nickname,
            @PathVariable Integer tagNumber,
            @AuthenticationPrincipal CustomUserDetails currentUser) {
        authService.deleteUser(nickname, tagNumber, currentUser);
        return ResponseEntity.noContent().build();
    }

    @GetMapping
    public ResponseEntity<List<UserResponse>> listUsers() {
        return ResponseEntity.ok(authService.listUsers());
    }
}
