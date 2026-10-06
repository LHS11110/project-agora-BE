package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.*;

/** Read-only previews use the same live document and group visibility as the canvas. */
@Service
public class CanvasPreviewService {
    private final CanvasInfoRepository repository;
    private final CanvasElasticsearchService elasticsearch;
    private final CanvasRedisDocumentReader redis;
    private static final Set<String> FIELDS = Set.of("kind", "x", "y", "width", "height", "rotation", "color",
            "text", "format", "code", "language", "filename", "formula", "columns", "rows", "src",
            "title", "url", "mediaType", "shapeType", "points", "strokeWidth", "from", "to", "bend",
            "startHead", "endHead", "fill", "opacity");
    public CanvasPreviewService(CanvasInfoRepository repository, CanvasElasticsearchService elasticsearch,
                                CanvasRedisDocumentReader redis) {
        this.repository = repository; this.elasticsearch = elasticsearch; this.redis = redis;
    }
    @Transactional
    public Map<String, Object> preview(Integer canvasId, CustomUserDetails user) {
        if (user == null || user.getUserId() == null) throw new CustomException(ErrorCode.UNAUTHORIZED);
        var info = repository.findByIdWithPessimisticLock(canvasId)
                .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND));
        CanvasDocument doc = Boolean.TRUE.equals(info.getIsCached()) ? redis.read(info)
                : elasticsearch.getCanvasDocumentById(canvasId)
                    .orElseThrow(() -> new CustomException(ErrorCode.CANVAS_NOT_FOUND));
        boolean systemAdmin = user.getUser() != null && user.getUser().getRole() == Role.ROLE_ADMIN;
        if (!systemAdmin && (doc.getPeople() == null || !doc.getPeople().contains(user.getUserId())))
            throw new CustomException(ErrorCode.ACCESS_DENIED);
        Set<String> groups = new HashSet<>();
        if (doc.getInnerGroup() != null) doc.getInnerGroup().forEach((group, members) -> {
            if (members != null && members.contains(user.getUserId())) groups.add(group);
        });
        boolean admin = systemAdmin || groups.contains("admin-group");
        Map<String, Object> items = new LinkedHashMap<>();
        if (doc.getItems() != null) doc.getItems().forEach((id, raw) -> {
            if (!(raw instanceof Map<?, ?> item) || (!admin && !allowed(item.get("permission"), groups))) return;
            Map<String, Object> clean = new LinkedHashMap<>();
            item.forEach((field, value) -> { if (field instanceof String key && FIELDS.contains(key)) clean.put(key, value); });
            items.put(id, clean);
        });
        items.entrySet().removeIf(entry -> entry.getValue() instanceof Map<?, ?> item
                && "connector".equals(item.get("kind"))
                && (!items.containsKey(item.get("from")) || !items.containsKey(item.get("to"))));
        return Map.of("items", items);
    }
    private boolean allowed(Object permission, Set<String> groups) {
        if (permission instanceof String group) return groups.contains(group);
        if (permission instanceof List<?> required) return required.stream().anyMatch(groups::contains);
        if (permission instanceof Map<?, ?> required) return required.entrySet().stream().anyMatch(entry ->
                groups.contains(entry.getKey()) && entry.getValue() instanceof Number level && level.doubleValue() > 0);
        return false;
    }
}
