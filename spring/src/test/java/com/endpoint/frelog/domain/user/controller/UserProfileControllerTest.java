package com.endpoint.frelog.domain.user.controller;

import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.service.UserProfileImageService;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.junit.jupiter.api.Test;
import java.util.Optional;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;

class UserProfileControllerTest {
    @Test void missingOptionalImageReturnsNoContent() {
        var users = mock(UserRepository.class);
        var images = mock(UserProfileImageService.class);
        var current = mock(CustomUserDetails.class);
        var user = mock(User.class);
        when(current.getUserId()).thenReturn(2L);
        when(user.isActive()).thenReturn(true);
        when(users.findById(2L)).thenReturn(Optional.of(user));
        var result = new UserProfileController(users, images).image(2L, current);
        assertEquals(204, result.getStatusCode().value());
        assertNull(result.getBody());
        assertEquals("no-store", result.getHeaders().getCacheControl());
    }
    @Test void uploadedImageStillReturnsPng() {
        var users = mock(UserRepository.class);
        var images = mock(UserProfileImageService.class);
        var current = mock(CustomUserDetails.class);
        var user = mock(User.class);
        when(current.getUserId()).thenReturn(2L);
        when(user.isActive()).thenReturn(true);
        when(users.findById(2L)).thenReturn(Optional.of(user));
        byte[] bytes = {1, 2, 3};
        when(images.read(2L)).thenReturn(bytes);
        var result = new UserProfileController(users, images).image(2L, current);
        assertEquals(200, result.getStatusCode().value());
        assertArrayEquals(bytes, result.getBody());
        assertEquals("image/png", result.getHeaders().getContentType().toString());
    }
}
