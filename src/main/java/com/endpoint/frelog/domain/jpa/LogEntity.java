package com.endpoint.frelog.domain.jpa;

import jakarta.persistence.*;
import lombok.AccessLevel;
import lombok.AllArgsConstructor;
import lombok.Builder;
import lombok.Getter;
import lombok.NoArgsConstructor;

import java.time.LocalDateTime;

@Entity
@Table(name = "application_logs")
@Getter
@Builder
@NoArgsConstructor(access = AccessLevel.PROTECTED)
@AllArgsConstructor
public class LogEntity {

    @Id
    @GeneratedValue(strategy = GenerationType.IDENTITY)
    private Long id;

    @Column(nullable = false, length = 100)
    private String serviceName;

    @Column(nullable = false, length = 20)
    private String level;

    @Column(columnDefinition = "NVARCHAR(MAX)")
    private String message;

    @Column(nullable = false)
    private LocalDateTime timestamp;

    @Column(columnDefinition = "NVARCHAR(MAX)")
    private String metadata;
}
