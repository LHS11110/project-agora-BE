package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonAnyGetter;
import com.fasterxml.jackson.annotation.JsonAnySetter;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * project-agora-DB 스키마 규격의 캔버스 배치 아이템 모델
 */
public class CanvasItemDocument {

    @JsonProperty("item-id")
    private Long itemId;

    @JsonProperty("type")
    private Integer type;

    @JsonProperty("pos")
    private List<Float> pos = new ArrayList<>();

    @JsonProperty("permission")
    private Map<String, Integer> permission = new LinkedHashMap<>();

    private Map<String, Object> additionalProperties = new LinkedHashMap<>();

    public CanvasItemDocument() {
        this.permission.put("admin-group", 7);
    }

    public CanvasItemDocument(Long itemId, Integer type, Float x, Float y) {
        this();
        this.itemId = itemId;
        this.type = type;
        this.pos.add(x);
        this.pos.add(y);
    }

    public Long getItemId() {
        return itemId;
    }

    public void setItemId(Long itemId) {
        this.itemId = itemId;
    }

    public Integer getType() {
        return type;
    }

    public void setType(Integer type) {
        this.type = type;
    }

    public List<Float> getPos() {
        return pos;
    }

    public void setPos(List<Float> pos) {
        this.pos = pos;
    }

    public Map<String, Integer> getPermission() {
        return permission;
    }

    public void setPermission(Map<String, Integer> permission) {
        this.permission = permission != null ? permission : new LinkedHashMap<>();
        this.permission.put("admin-group", 7);
    }

    @JsonAnyGetter
    public Map<String, Object> getAdditionalProperties() {
        return additionalProperties;
    }

    @JsonAnySetter
    public void setAdditionalProperty(String name, Object value) {
        this.additionalProperties.put(name, value);
    }
}
