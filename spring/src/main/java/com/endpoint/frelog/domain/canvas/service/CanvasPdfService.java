package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.springframework.stereotype.Service;
import org.springframework.web.multipart.MultipartFile;
import java.io.IOException;
import java.io.InputStream;
import java.nio.charset.StandardCharsets;
import java.nio.file.*;
import java.util.Map;
import java.util.UUID;

@Service
public class CanvasPdfService {
    private final CanvasResourceService resources;
    public CanvasPdfService(CanvasResourceService resources) { this.resources = resources; }
    public Map<String, String> save(Integer canvasId, MultipartFile file, MultipartFile thumbnail) {
        if (file == null || file.isEmpty()) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "PDF 파일을 선택하세요.");
        Path temporary = null, poster = null;
        try {
            byte[] header;
            try (InputStream source = file.getInputStream()) { header = source.readNBytes(1024); }
            if (!new String(header, StandardCharsets.ISO_8859_1).contains("%PDF-")) throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "올바른 PDF 파일을 선택하세요.");
            if (thumbnail != null && thumbnail.getSize() > 2L * 1024 * 1024) throw new CustomException(ErrorCode.PAYLOAD_TOO_LARGE);
            byte[] png = thumbnail == null || thumbnail.isEmpty() ? null : resources.normalizePng(thumbnail.getBytes());
            String id = UUID.randomUUID().toString();
            Path directory = resources.getCanvasDirectory(canvasId).resolve("pdfs");
            Files.createDirectories(directory);
            temporary = Files.createTempFile(directory, "upload-", ".tmp");
            file.transferTo(temporary);
            poster = directory.resolve(id + ".png");
            if (png != null) Files.write(poster, png, StandardOpenOption.CREATE_NEW);
            Files.move(temporary, directory.resolve(id + ".pdf"), StandardCopyOption.ATOMIC_MOVE);
            String url = "/api/canvases/" + canvasId + "/pdfs/" + id;
            return Map.of("src", url, "thumbnail", png == null ? "" : url + "/thumbnail");
        } catch (IOException | RuntimeException failure) {
            for (Path path : new Path[]{temporary, poster}) if (path != null) try { Files.deleteIfExists(path); } catch (IOException ignored) { }
            if (failure instanceof CustomException custom) throw custom;
            throw new CustomException(ErrorCode.INTERNAL_SERVER_ERROR, "PDF를 저장하지 못했습니다.");
        }
    }
    public Path path(Integer canvasId, String id, boolean thumbnail) {
        try { if (!UUID.fromString(id).toString().equals(id)) throw new IllegalArgumentException(); }
        catch (IllegalArgumentException failure) { throw new CustomException(ErrorCode.INVALID_INPUT_VALUE, "PDF ID가 올바르지 않습니다."); }
        return resources.getCanvasDirectory(canvasId).resolve("pdfs").resolve(id + (thumbnail ? ".png" : ".pdf"));
    }
}
