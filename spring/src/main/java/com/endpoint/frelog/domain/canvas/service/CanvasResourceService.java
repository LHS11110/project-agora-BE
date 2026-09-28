package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;

import javax.imageio.ImageIO;
import javax.imageio.ImageReader;
import javax.imageio.stream.ImageInputStream;
import java.awt.image.BufferedImage;
import java.io.ByteArrayInputStream;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.nio.file.AtomicMoveNotSupportedException;
import java.nio.file.Files;
import java.nio.file.Path;
import java.nio.file.Paths;
import java.nio.file.StandardCopyOption;
import java.util.Iterator;
import java.util.List;
import java.util.Locale;

@Service
public class CanvasResourceService {

    private static final Logger log = LoggerFactory.getLogger(CanvasResourceService.class);
    private static final long MAX_UPLOAD_BYTES = 5L * 1024 * 1024;
    private static final int MAX_IMAGE_DIMENSION = 2048;
    private static final long MAX_IMAGE_PIXELS = 4_194_304L;
    private static final String IMAGE_URL_PREFIX = "/api/canvases/";
    private static final byte[] DEFAULT_PNG = new byte[]{
            (byte) 0x89, 0x50, 0x4E, 0x47, 0x0D, 0x0A, 0x1A, 0x0A, 0x00, 0x00, 0x00, 0x0D, 0x49, 0x48, 0x44, 0x52,
            0x00, 0x00, 0x00, 0x01, 0x00, 0x00, 0x00, 0x01, 0x08, 0x06, 0x00, 0x00, 0x00, 0x1F, 0x15, (byte) 0xC4,
            (byte) 0x89, 0x00, 0x00, 0x00, 0x0A, 0x49, 0x44, 0x41, 0x54, 0x78, (byte) 0x9C, 0x63, 0x00, 0x01, 0x00, 0x00,
            0x05, 0x00, 0x01, 0x0D, 0x0A, 0x2D, (byte) 0xB4, 0x00, 0x00, 0x00, 0x00, 0x49, 0x45, 0x4E, 0x44, (byte) 0xAE,
            0x42, 0x60, (byte) 0x82
    };

    private final Path baseResourceDir;

    public CanvasResourceService() {
        String userHome = System.getProperty("user.home", "/home/ubuntu");
        this.baseResourceDir = Paths.get(userHome, "project-agora", "canvas-resource");
        try {
            Files.createDirectories(this.baseResourceDir);
        } catch (IOException e) {
            log.warn("기본 캔버스 리소스 디렉터리 생성 실패", e);
        }
    }

    public Path getCanvasDirectory(Integer canvasId) {
        return baseResourceDir.resolve(String.valueOf(canvasId));
    }

    /** Accept PNG/JPEG only, verify the actual bytes, and store a normalized PNG. */
    public String saveRepresentativeImage(Integer canvasId, MultipartFile file) {
        if (file == null || file.isEmpty()) return saveDefaultImage(canvasId);
        if (file.getSize() > MAX_UPLOAD_BYTES) throw payloadTooLarge();

        try {
            return storePng(canvasId, normalizePng(file.getBytes()));
        } catch (IOException e) {
            log.error("캔버스 #{} 대표 이미지 저장 실패", canvasId, e);
            throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR);
        }
    }

    /** Compatibility entry point; the supplied filename extension is never trusted. */
    public String saveRepresentativeImageBytes(Integer canvasId, byte[] imageBytes, String ignoredExtension) {
        return storePng(canvasId, normalizePng(imageBytes));
    }

    public String saveDefaultImage(Integer canvasId) {
        Path target = getCanvasDirectory(canvasId).resolve("representative.png");
        if (Files.isRegularFile(target)) return imageUrl(canvasId);
        return storePng(canvasId, DEFAULT_PNG);
    }

    private byte[] normalizePng(byte[] source) {
        if (source == null || source.length == 0) {
            throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "PNG 또는 JPEG 이미지 파일을 선택하세요.");
        }
        if (source.length > MAX_UPLOAD_BYTES) throw payloadTooLarge();

        try (ImageInputStream input = ImageIO.createImageInputStream(new ByteArrayInputStream(source))) {
            if (input == null) throw invalidImage();
            Iterator<ImageReader> readers = ImageIO.getImageReaders(input);
            if (!readers.hasNext()) throw invalidImage();

            ImageReader reader = readers.next();
            try {
                reader.setInput(input, true, true);
                String format = reader.getFormatName().toLowerCase(Locale.ROOT);
                if (!format.equals("png") && !format.equals("jpeg") && !format.equals("jpg")) {
                    throw invalidImage();
                }

                int width = reader.getWidth(0);
                int height = reader.getHeight(0);
                if (width < 1 || height < 1 || width > MAX_IMAGE_DIMENSION || height > MAX_IMAGE_DIMENSION
                        || (long) width * height > MAX_IMAGE_PIXELS) {
                    throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,
                            "이미지 크기는 가로·세로 2048픽셀, 전체 4백만 픽셀 이하여야 합니다.");
                }

                BufferedImage decoded = reader.read(0);
                if (decoded == null) throw invalidImage();
                try (ByteArrayOutputStream output = new ByteArrayOutputStream()) {
                    if (!ImageIO.write(decoded, "png", output)) throw invalidImage();
                    byte[] normalized = output.toByteArray();
                    if (normalized.length > MAX_UPLOAD_BYTES) throw payloadTooLarge();
                    return normalized;
                }
            } finally {
                reader.dispose();
            }
        } catch (CustomException e) {
            throw e;
        } catch (IOException | RuntimeException e) {
            throw invalidImage();
        }
    }

    private String storePng(Integer canvasId, byte[] png) {
        Path canvasDir = getCanvasDirectory(canvasId);
        Path target = canvasDir.resolve("representative.png");
        Path temporary = null;
        try {
            Files.createDirectories(canvasDir);
            temporary = Files.createTempFile(canvasDir, ".representative-", ".tmp");
            Files.write(temporary, png);
            try {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING, StandardCopyOption.ATOMIC_MOVE);
            } catch (AtomicMoveNotSupportedException e) {
                Files.move(temporary, target, StandardCopyOption.REPLACE_EXISTING);
            }
            removeLegacyRepresentativeFiles(canvasDir, target);
            log.info("캔버스 #{} 대표 이미지 저장 완료", canvasId);
            return imageUrl(canvasId);
        } catch (IOException e) {
            log.error("캔버스 #{} 대표 이미지 저장 실패", canvasId, e);
            throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR);
        } finally {
            if (temporary != null) {
                try {
                    Files.deleteIfExists(temporary);
                } catch (IOException e) {
                    log.warn("캔버스 #{} 임시 대표 이미지 파일 정리 실패", canvasId, e);
                }
            }
        }
    }

    private void removeLegacyRepresentativeFiles(Path canvasDir, Path currentFile) {
        try (var stream = Files.list(canvasDir)) {
            stream.filter(path -> !path.equals(currentFile)
                            && path.getFileName().toString().startsWith("representative"))
                    .forEach(path -> {
                        try {
                            Files.deleteIfExists(path);
                        } catch (IOException e) {
                            log.warn("레거시 대표 이미지 파일 정리 실패: {}", path.getFileName());
                        }
                    });
        } catch (IOException e) {
            log.warn("레거시 대표 이미지 파일 목록 조회 실패", e);
        }
    }

    private CustomException invalidImage() {
        return new CustomException(ErrorCode.INVALID_INPUT_VALUE, "PNG 또는 JPEG 이미지 파일만 업로드할 수 있습니다.");
    }

    private CustomException payloadTooLarge() {
        return new CustomException(ErrorCode.PAYLOAD_TOO_LARGE);
    }

    private String imageUrl(Integer canvasId) {
        return IMAGE_URL_PREFIX + canvasId + "/image";
    }

    public Path getRepresentativeImageFile(Integer canvasId) {
        Path imagePath = getCanvasDirectory(canvasId).resolve("representative.png");
        if (Files.isRegularFile(imagePath)) return imagePath;

        // Upgrade images created by the previous implementation, which kept
        // the client-provided extension. Validate and re-encode before serving.
        Path canvasDir = getCanvasDirectory(canvasId);
        for (String extension : List.of("jpg", "jpeg", "JPG", "JPEG")) {
            Path legacyPath = canvasDir.resolve("representative." + extension);
            if (!Files.isRegularFile(legacyPath)) continue;
            try {
                long size = Files.size(legacyPath);
                if (size < 1 || size > MAX_UPLOAD_BYTES) continue;
                storePng(canvasId, normalizePng(Files.readAllBytes(legacyPath)));
                return imagePath;
            } catch (IOException | CustomException e) {
                log.warn("캔버스 #{} 레거시 대표 이미지 검증 실패", canvasId);
            }
        }
        return null;
    }

    /** Delete all files under ~/project-agora/canvas-resource/{canvas-id}. */
    public void deleteCanvasResourceDirectory(Integer canvasId) {
        Path canvasDir = getCanvasDirectory(canvasId);
        if (!Files.exists(canvasDir)) return;

        try (var stream = Files.walk(canvasDir)) {
            for (Path path : stream.sorted(java.util.Comparator.reverseOrder()).toList()) {
                Files.deleteIfExists(path);
            }
            log.info("캔버스 #{} 비정형 리소스 디렉터리 삭제 완료", canvasId);
        } catch (IOException e) {
            log.warn("캔버스 #{} 리소스 디렉터리 삭제 실패", canvasId, e);
        }
    }
}
