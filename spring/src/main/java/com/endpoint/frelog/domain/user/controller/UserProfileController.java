package com.endpoint.frelog.domain.user.controller;

import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.service.UserProfileImageService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.http.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import org.springframework.transaction.annotation.Transactional;
import org.springframework.web.bind.annotation.*;
import org.springframework.web.multipart.MultipartFile;
import java.util.Map;

@RestController
@RequestMapping("/api/users")
public class UserProfileController {
    private final UserRepository users;
    private final UserProfileImageService images;
    public UserProfileController(UserRepository users, UserProfileImageService images) { this.users = users; this.images = images; }
    private User active(Long id) {
        User user = users.findById(id).orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));
        if (!user.isActive()) throw new CustomException(ErrorCode.USER_NOT_FOUND);
        return user;
    }
    private Long self(CustomUserDetails current) {
        if (current == null || current.getUserId() == null) throw new CustomException(ErrorCode.UNAUTHORIZED);
        active(current.getUserId());
        return current.getUserId();
    }
    @GetMapping("/profiles/{nickname}/{tagNumber}")
    @Transactional(readOnly = true)
    public Map<String, Object> profile(@PathVariable String nickname, @PathVariable Integer tagNumber, @AuthenticationPrincipal CustomUserDetails current) {
        self(current);
        User user = users.findByNicknameAndTagNumber(nickname, tagNumber).orElseThrow(() -> new CustomException(ErrorCode.USER_NOT_FOUND));
        if (!user.isActive()) throw new CustomException(ErrorCode.USER_NOT_FOUND);
        return Map.of("user_id", user.getUserId(), "nickname", user.getNickname(), "tag_number", user.getTagNumber(), "profile_image", images.url(user.getUserId()));
    }
    @GetMapping("/profile-images/{id}")
    public ResponseEntity<byte[]> image(@PathVariable Long id, @AuthenticationPrincipal CustomUserDetails current) {
        self(current); active(id);
        return ResponseEntity.ok().cacheControl(CacheControl.noStore()).contentType(MediaType.IMAGE_PNG).body(images.read(id));
    }
    @PutMapping(value = "/me/profile-image", consumes = MediaType.MULTIPART_FORM_DATA_VALUE)
    public Map<String, String> upload(@RequestPart("image") MultipartFile file, @AuthenticationPrincipal CustomUserDetails current) {
        Long id = self(current); images.save(id, file);
        return Map.of("profile_image", images.url(id));
    }
    @DeleteMapping("/me/profile-image")
    public ResponseEntity<Void> remove(@AuthenticationPrincipal CustomUserDetails current) {
        images.remove(self(current)); return ResponseEntity.noContent().build();
    }
}
