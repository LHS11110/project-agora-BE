package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonAlias;
import com.fasterxml.jackson.annotation.JsonProperty;

import java.util.List;
import java.util.Map;

public record UpdateCanvasDocumentRequest(
        @JsonProperty("canvas-password")
        @JsonAlias({"canvas-password", "canvasPassword"})
        String canvasPassword,

        @JsonProperty("peoples")
        @JsonAlias({"peoples"})
        List<Long> peoples,

        @JsonProperty("inner-group")
        @JsonAlias({"inner-group", "innerGroup"})
        Map<String, List<Long>> innerGroup,

        @JsonProperty("items")
        @JsonAlias({"items"})
        Map<String, Object> items,

        @JsonProperty("init-group")
        @JsonAlias({"init-group", "initGroup"})
        String initGroup
) {
}
