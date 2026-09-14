package com.endpoint.frelog.domain.canvas.dto;

import com.fasterxml.jackson.annotation.JsonProperty;

public record CanvasSummaryResponse(
        @JsonProperty("canvas_id")
        Integer canvasId,

        @JsonProperty("image")
        String image,

        @JsonProperty("description")
        String description,

        @JsonProperty("canvas_name")
        String canvasName,

        @JsonProperty("user_count")
        Integer userCount
) {
    public static CanvasSummaryResponse of(CanvasDocument doc, String imagePath) {
        int count = (doc.getPeople() != null) ? doc.getPeople().size() : 0;
        return new CanvasSummaryResponse(
                doc.getCanvasId(),
                imagePath,
                doc.getDescription() != null ? doc.getDescription() : "",
                doc.getCanvasName(),
                count
        );
    }
}
