package com.endpoint.frelog.domain.canvas.service;

import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;

import java.io.File;
import java.io.IOException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.Comparator;

@Service
public class CanvasResourceService {

    private static final Logger log = LoggerFactory.getLogger(CanvasResourceService.class);

    private final Path baseResourceDir;

    public CanvasResourceService() {
        String userHome = System.getProperty("user.home", "/home/ubuntu");
        this.baseResourceDir = Paths.get(userHome, "project-agora", "canvas-resource");
        try {
            Files.createDirectories(this.baseResourceDir);
        } catch (IOException e) {
            log.warn("기본 캔버스 리소스 디렉터리 생성 실패: {}", e.getMessage());
        }
    }

    public Path getCanvasDirectory(Integer canvasId) {
        return baseResourceDir.resolve(String.valueOf(canvasId));
    }

    /**
     * 대표 이미지 저장
     */
    public String saveRepresentativeImage(Integer canvasId, MultipartFile file) {
        if (file == null || file.isEmpty()) {
            return saveDefaultImage(canvasId);
        }

        try {
            Path canvasDir = getCanvasDirectory(canvasId);
            Files.createDirectories(canvasDir);

            String originalName = file.getOriginalFilename();
            String ext = "png";
            if (originalName != null && originalName.contains(".")) {
                ext = originalName.substring(originalName.lastIndexOf('.') + 1);
            }

            Path targetPath = canvasDir.resolve("representative." + ext);
            Files.copy(file.getInputStream(), targetPath, StandardCopyOption.REPLACE_EXISTING);
            log.info("캔버스 #{} 대표 이미지 저장 완료: {}", canvasId, targetPath);
            return "/api/canvases/" + canvasId + "/image";
        } catch (IOException e) {
            log.warn("캔버스 #{} 대표 이미지 저장 실패: {}", canvasId, e.getMessage());
            return "/api/canvases/" + canvasId + "/image";
        }
    }

    public String saveRepresentativeImageBytes(Integer canvasId, byte[] imageBytes, String ext) {
        try {
            Path canvasDir = getCanvasDirectory(canvasId);
            Files.createDirectories(canvasDir);

            Path targetPath = canvasDir.resolve("representative." + (ext != null ? ext : "png"));
            Files.write(targetPath, imageBytes);
            log.info("캔버스 #{} 대표 이미지(byte) 저장 완료: {}", canvasId, targetPath);
            return "/api/canvases/" + canvasId + "/image";
        } catch (IOException e) {
            log.warn("캔버스 #{} 대표 이미지(byte) 저장 실패: {}", canvasId, e.getMessage());
            return "/api/canvases/" + canvasId + "/image";
        }
    }

    public String saveDefaultImage(Integer canvasId) {
        try {
            Path canvasDir = getCanvasDirectory(canvasId);
            Files.createDirectories(canvasDir);

            Path targetPath = canvasDir.resolve("representative.png");
            if (!Files.exists(targetPath)) {
                // Create a minimal 1x1 transparent png
                byte[] defaultPng = new byte[]{
                        (byte) 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
                        0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, (byte) 0xC4,
                        (byte) 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, (byte) 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
                        0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, (byte) 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, (byte) 0xAE,
                        0x42, 0x60, (byte) 0x82
                };
                Files.write(targetPath, defaultPng);
            }
            return "/api/canvases/" + canvasId + "/image";
        } catch (IOException e) {
            return "/api/canvases/" + canvasId + "/image";
        }
    }

    public Path getRepresentativeImageFile(Integer canvasId) {
        Path canvasDir = getCanvasDirectory(canvasId);
        if (!Files.exists(canvasDir)) {
            return null;
        }

        try (var stream = Files.list(canvasDir)) {
            return stream
                    .filter(p -> p.getFileName().toString().startsWith("representative"))
                    .findFirst()
                    .orElse(null);
        } catch (IOException e) {
            return null;
        }
    }

    /**
     * 캔버스 삭제 시 ~/project-agora/canvas-resource/{canvas-id} 디렉터리 일괄 정리
     */
    public void deleteCanvasResourceDirectory(Integer canvasId) {
        Path canvasDir = getCanvasDirectory(canvasId);
        if (!Files.exists(canvasDir)) {
            return;
        }

        try {
            try (var stream = Files.walk(canvasDir)) {
                stream.sorted(Comparator.reverseOrder())
                        .map(Path::toFile)
                        .forEach(File::delete);
            }
            log.info("캔버스 #{} 비정형 리소스 디렉터리 삭제 완료: {}", canvasId, canvasDir);
        } catch (IOException e) {
            log.warn("캔버스 #{} 리소스 디렉터리 삭제 실패: {}", canvasId, e.getMessage());
        }
    }
}
