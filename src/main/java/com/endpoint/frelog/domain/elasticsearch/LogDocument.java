package com.endpoint.frelog.domain.elasticsearch;

import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Getter;
import lombok.NoArgsConstructor;
import org.springframework.data.annotation.Id;
import org.springframework.data.elasticsearch.annotations.DateFormat;
import org.springframework.data.elasticsearch.annotations.Document;
import org.springframework.data.elasticsearch.annotations.Field;
import org.springframework.data.elasticsearch.annotations.FieldType;

import java.time.LocalDateTime;
import java.util.Map;

@Document(indexName = "application-logs")
@Getter
@Builder
@NoArgsConstructor
@AllArgsConstructor
public class LogDocument {

    @Id
    private String id;

    @Field(type = FieldType.Keyword)
    private String serviceName;

    @Field(type = FieldType.Keyword)
    private String level;

    @Field(type = FieldType.Text)
    private String message;

    @Field(type = FieldType.Date, format = DateFormat.date_hour_minute_second)
    private LocalDateTime timestamp;

    @Field(type = FieldType.Object)
    private Map<String, Object> metadata;
}
