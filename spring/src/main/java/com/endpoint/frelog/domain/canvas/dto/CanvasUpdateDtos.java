package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonProperty;
import jakarta.validation.constraints.NotBlank;
import jakarta.validation.constraints.NotNull;
import java.util.List;

public class CanvasUpdateDtos {

    public record UpdateNameRequest(
            @NotBlank(message = "캔버스 이름은 비어있을 수 없습니다.")
            @JsonProperty("canvas_name")
            @com.fasterxml.jackson.annotation.JsonAlias({"canvasName", "name"})
            String canvasName
    ) {}

    public record UpdateOwnerRequest(
            @NotNull(message = "소유자 사용자 ID는 필수입니다.")
            @JsonProperty("admin_user_id")
            @com.fasterxml.jackson.annotation.JsonAlias({"adminUserId", "userId", "user_id"})
            Long adminUserId
    ) {}

    public record UpdateDescriptionRequest(
            @JsonProperty("description")
            String description
    ) {}

    public record UpdatePasswordRequest(
            @JsonProperty("canvas_password")
            @com.fasterxml.jackson.annotation.JsonAlias({"canvasPassword", "password"})
            String canvasPassword
    ) {}

    public record PeopleRequest(
            @NotNull(message = "사용자 ID는 필수입니다.")
            @JsonProperty("user_id")
            @com.fasterxml.jackson.annotation.JsonAlias({"userId", "targetUserId", "target_user_id"})
            Long userId
    ) {}

    public record ParticipantHandleRequest(
            @NotBlank String nickname,
            @NotNull @JsonProperty("tag_number") Integer tagNumber
    ) {}

    public record ParticipantResponse(
            String nickname,
            @JsonProperty("tag_number") Integer tagNumber
    ) {}

    public record SettingsResponse(
            @JsonProperty("canvas_id") Integer canvasId,
            @JsonProperty("canvas_name") String canvasName,
            String description,
            @JsonProperty("password_protected") boolean passwordProtected,
            @JsonProperty("settings_revision") Long settingsRevision,
            List<ParticipantResponse> participants
    ) {}

    public record GroupRequest(
            @NotBlank(message = "그룹명은 비어있을 수 없습니다.")
            @JsonProperty("group_name")
            @com.fasterxml.jackson.annotation.JsonAlias({"groupName", "name"})
            String groupName
    ) {}

    public record GroupMemberRequest(
            @NotNull(message = "사용자 ID는 필수입니다.")
            @JsonProperty("user_id")
            @com.fasterxml.jackson.annotation.JsonAlias({"userId", "targetUserId", "target_user_id"})
            Long userId
    ) {}

    public record InitGroupRequest(
            @NotBlank(message = "초기 그룹명은 비어있을 수 없습니다.")
            @JsonProperty("init_group")
            @com.fasterxml.jackson.annotation.JsonAlias({"initGroup"})
            String initGroup
    ) {}

    public record AccessRequest(
            @NotNull(message = "캔버스 ID는 필수입니다.")
            @JsonProperty("canvas_id")
            @com.fasterxml.jackson.annotation.JsonAlias({"canvasId"})
            Integer canvasId
    ) {}

    public record AccessPasswordRequest(String password) {}

    public record AccessResponse(
            @JsonProperty("server_id")
            Integer serverId,

            @JsonProperty("ws_port")
            String wsPort,

            @JsonProperty("canvas_access_token")
            String canvasAccessToken
    ) {
    }
}
