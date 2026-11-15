package com.endpoint.frelog.domain.canvas.service;

import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.dto.DocumentScope;
import com.endpoint.frelog.domain.canvas.entity.SpaceInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.canvas.repository.WorkspaceInfoRepository;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.beans.factory.annotation.Autowired;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.util.*;

/** Read-only previews use the same live document and group visibility as the canvas or workspace. */
@Service
public class CanvasPreviewService {
    private final CanvasInfoRepository repository;
    private final WorkspaceInfoRepository workspaces;
    private final CanvasElasticsearchService elasticsearch;
    private final CanvasRedisDocumentReader redis;
    private static final Set<String> FIELDS = Set.of("kind", "x", "y", "width", "height", "rotation", "color",
            "text", "format", "code", "language", "filename", "formula", "columns", "rows", "src", "thumbnail",
            "title", "url", "mediaType", "shapeType", "points", "strokeWidth", "from", "to", "fromPort", "toPort", "bend",
            "startHead", "endHead", "fill", "opacity", "members", "groupTitle");

    public CanvasPreviewService(CanvasInfoRepository repository, CanvasElasticsearchService elasticsearch,
                                CanvasRedisDocumentReader redis) {
        this(repository, null, elasticsearch, redis);
    }

    @Autowired
    public CanvasPreviewService(CanvasInfoRepository repository, WorkspaceInfoRepository workspaces,
                                CanvasElasticsearchService elasticsearch, CanvasRedisDocumentReader redis) {
        this.repository = repository; this.workspaces = workspaces; this.elasticsearch = elasticsearch; this.redis = redis;
    }

    @Transactional
    public Map<String, Object> preview(Integer canvasId, CustomUserDetails user) {
        return preview(DocumentScope.CANVAS, canvasId, user);
    }

    @Transactional
    public Map<String, Object> preview(DocumentScope scope, Integer id, CustomUserDetails user) {
        if (user == null || user.getUserId() == null) throw new CustomException(ErrorCode.UNAUTHORIZED);
        SpaceInfo info = scope == DocumentScope.WORKSPACE
                ? (workspaces == null ? Optional.<SpaceInfo>empty() : workspaces.findByIdWithPessimisticLock(id).map(SpaceInfo.class::cast))
                        .orElseThrow(() -> new CustomException(scope.notFoundError()))
                : repository.findByIdWithPessimisticLock(id)
                        .orElseThrow(() -> new CustomException(scope.notFoundError()));
        CanvasDocument doc = Boolean.TRUE.equals(info.getIsCached()) ? redis.read(scope, info)
                : elasticsearch.getDocumentById(scope, id)
                    .orElseThrow(() -> new CustomException(scope.notFoundError()));
        if (!CanvasVisibility.allows(doc, user))
            throw new CustomException(ErrorCode.ACCESS_DENIED);
        Set<String> groups = new HashSet<>();
        if (doc.getInnerGroup() != null) doc.getInnerGroup().forEach((group, members) -> {
            if (members != null && members.contains(user.getUserId()) && CanvasGroupPermissions.group(doc,group,"read")) groups.add(group);
        });
        if ("public".equals(doc.getVisibility()) && doc.getInitGroup() != null && !doc.getInitGroup().equals("admin-group") && CanvasGroupPermissions.group(doc,doc.getInitGroup(),"read")) groups.add(doc.getInitGroup());
        boolean admin = CanvasGroupPermissions.admin(doc,user.getUserId());
        var index=new CanvasGroupBits(doc);final long readMask=index.mask(groups);
        Map<String, Object> items = new LinkedHashMap<>();
        if (doc.getItems() != null) doc.getItems().forEach((itemId, raw) -> {
            if (!(raw instanceof Map<?, ?> item) || (!admin && !(index.valid?(readMask&index.permissionMask(item.get("permission")))!=0:allowed(item.get("permission"), groups)))) return;
            Map<String, Object> clean = new LinkedHashMap<>();
            item.forEach((field, value) -> { if (field instanceof String key && FIELDS.contains(key)) clean.put(key, value); });
            items.put(itemId, clean);
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
