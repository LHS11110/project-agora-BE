package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonInclude;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.ArrayList;
import java.util.LinkedHashMap;
import java.util.List;
import java.util.Map;

/**
 * project-agora-DB 및 Elasticsearch 'canvas' 인덱스 규격을 반영한 도큐먼트 모델
 */
@JsonInclude(JsonInclude.Include.ALWAYS)
public class CanvasDocument {

    @JsonProperty("canvas-name")
    private String canvasName;

    @JsonProperty("canvas-id")
    private Integer canvasId;

    @JsonProperty("admin")
    private Long admin;

    @JsonProperty("canvas-password")
    private String canvasPassword;

    @JsonProperty("peoples")
    private List<Long> peoples = new ArrayList<>();

    @JsonProperty("inner-group")
    private Map<String, List<Long>> innerGroup = new LinkedHashMap<>();

    @JsonProperty("items")
    private Map<String, Object> items = new LinkedHashMap<>();

    @JsonProperty("init-group")
    private String initGroup = "default";

    public CanvasDocument() {
    }

    public CanvasDocument(String canvasName, Integer canvasId, Long admin, String canvasPassword, String initGroup) {
        this.canvasName = canvasName;
        this.canvasId = canvasId;
        this.admin = admin;
        this.canvasPassword = canvasPassword;
        this.initGroup = (initGroup != null && !initGroup.isBlank()) ? initGroup : "default";

        if (admin != null) {
            this.peoples.add(admin);
            List<Long> adminList = new ArrayList<>();
            adminList.add(admin);
            this.innerGroup.put("admin-group", adminList);
            if (!this.innerGroup.containsKey(this.initGroup)) {
                this.innerGroup.put(this.initGroup, new ArrayList<>());
            }
        }
    }

    public String getCanvasName() {
        return canvasName;
    }

    public void setCanvasName(String canvasName) {
        this.canvasName = canvasName;
    }

    public Integer getCanvasId() {
        return canvasId;
    }

    public void setCanvasId(Integer canvasId) {
        this.canvasId = canvasId;
    }

    public Long getAdmin() {
        return admin;
    }

    public void setAdmin(Long admin) {
        this.admin = admin;
    }

    public String getCanvasPassword() {
        return canvasPassword;
    }

    public void setCanvasPassword(String canvasPassword) {
        this.canvasPassword = canvasPassword;
    }

    public List<Long> getPeoples() {
        return peoples;
    }

    public void setPeoples(List<Long> peoples) {
        this.peoples = peoples != null ? peoples : new ArrayList<>();
    }

    public Map<String, List<Long>> getInnerGroup() {
        return innerGroup;
    }

    public void setInnerGroup(Map<String, List<Long>> innerGroup) {
        this.innerGroup = innerGroup != null ? innerGroup : new LinkedHashMap<>();
    }

    public Map<String, Object> getItems() {
        return items;
    }

    public void setItems(Map<String, Object> items) {
        this.items = items != null ? items : new LinkedHashMap<>();
    }

    public String getInitGroup() {
        return initGroup;
    }

    public void setInitGroup(String initGroup) {
        this.initGroup = initGroup;
    }
}
