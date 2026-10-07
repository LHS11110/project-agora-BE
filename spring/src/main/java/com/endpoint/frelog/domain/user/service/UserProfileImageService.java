package com.endpoint.frelog.domain.user.service;

import com.endpoint.frelog.domain.canvas.service.CanvasResourceService;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;
import java.io.IOException;
import java.nio.file.*;

@Service
public class UserProfileImageService {
    private final CanvasResourceService images;
    private final Path directory = Paths.get(System.getProperty("user.home"), "project-agora", "canvas-resource", "profiles");
    public UserProfileImageService(CanvasResourceService images) { this.images = images; }
    public String url(Long id) { return "/api/users/profile-images/" + id; }
    public void save(Long id, MultipartFile file) {
        if (file == null || file.isEmpty()) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "이미지를 선택해주세요.");
        if (file.getSize() > 5L * 1024 * 1024) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "이미지는 5MB 이하여야 합니다.");
        Path temporary = null;
        try {
            byte[] png = images.normalizePng(file.getBytes());
            Files.createDirectories(directory);
            temporary = Files.createTempFile(directory, "avatar-", ".tmp");
            Files.write(temporary, png);
            Path target = directory.resolve(id + ".png");
            try { Files.move(temporary, target, StandardCopyOption.ATOMIC_MOVE, StandardCopyOption.REPLACE_EXISTING); }
            catch (AtomicMoveNotSupportedException ignored) { Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING); }
        } catch (IOException failure) { throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR); }
        finally { if (temporary != null) try { Files.deleteIfExists(temporary); } catch (IOException ignored) {} }
    }
    public byte[] read(Long id) {
        Path target = directory.resolve(id + ".png");
        if (!Files.isRegularFile(target)) throw new CustomException(ErrorCode.USER_NOT_FOUND, "프로필 이미지가 없습니다.");
        try { return Files.readAllBytes(target); }
        catch (IOException failure) { throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR); }
    }
    public void remove(Long id) {
        try { Files.deleteIfExists(directory.resolve(id + ".png")); }
        catch (IOException failure) { throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR); }
    }
}
