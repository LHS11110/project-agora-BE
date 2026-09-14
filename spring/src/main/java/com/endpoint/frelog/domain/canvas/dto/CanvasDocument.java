package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonIgnoreProperties;
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
@JsonIgnoreProperties(ignoreUnknown = true)
public class CanvasDocument {

    @JsonProperty("canvas-name")
    @JsonAlias({"canvasName", "canvas_name"})
    private String canvasName;

    @JsonProperty("canvas-id")
    @JsonAlias({"canvasId", "canvas_id"})
    private Integer canvasId;

    @JsonProperty("admin-user-id")
    @JsonAlias({"admin", "adminUserId", "admin_user_id"})
    private Long adminUserId;

    @JsonProperty("description")
    private String description = "";

    @JsonProperty("canvas-password-hash")
    @JsonAlias({"canvasPassword", "canvas_password_hash", "canvas-password"})
    private String canvasPasswordHash;

    @JsonProperty("people")
    @JsonAlias({"peoples"})
    private List<Long> people = new ArrayList<>();

    @JsonProperty("inner-group")
    @JsonAlias({"innerGroup", "inner_group"})
    private Map<String, List<Long>> innerGroup = new LinkedHashMap<>();

    @JsonProperty("items")
    private Map<String, Object> items = new LinkedHashMap<>();

    @JsonProperty("init-group")
    @JsonAlias({"initGroup", "init_group"})
    private String initGroup = "default";

    public CanvasDocument() {
    }

    public CanvasDocument(String canvasName, Integer canvasId, Long adminUserId, String canvasPasswordHash, String initGroup) {
        this.canvasName = canvasName;
        this.canvasId = canvasId;
        this.adminUserId = adminUserId;
        this.canvasPasswordHash = canvasPasswordHash;
        this.initGroup = (initGroup != null && !initGroup.isBlank()) ? initGroup : "default";

        if (adminUserId != null) {
            this.people.add(adminUserId);
            List<Long> adminList = new ArrayList<>();
            adminList.add(adminUserId);
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

    public Long getAdminUserId() {
        return adminUserId;
    }

    public void setAdminUserId(Long adminUserId) {
        this.adminUserId = adminUserId;
    }

    // Compatibility alias
    public Long getAdmin() {
        return adminUserId;
    }

    public void setAdmin(Long admin) {
        this.adminUserId = admin;
    }

    public String getDescription() {
        return description;
    }

    public void setDescription(String description) {
        this.description = description;
    }

    public String getCanvasPasswordHash() {
        return canvasPasswordHash;
    }

    public void setCanvasPasswordHash(String canvasPasswordHash) {
        this.canvasPasswordHash = canvasPasswordHash;
    }

    // Compatibility alias
    public String getCanvasPassword() {
        return canvasPasswordHash;
    }

    public void setCanvasPassword(String canvasPassword) {
        this.canvasPasswordHash = canvasPassword;
    }

    public List<Long> getPeople() {
        return people;
    }

    public void setPeople(List<Long> people) {
        this.people = people != null ? people : new ArrayList<>();
    }

    // Compatibility alias
    public List<Long> getPeoples() {
        return people;
    }

    public void setPeoples(List<Long> peoples) {
        this.people = peoples != null ? peoples : new ArrayList<>();
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
