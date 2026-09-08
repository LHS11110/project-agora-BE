package com.endpoint.frelog.scheduler;

import com.endpoint.frelog.domain.elasticsearch.DataRecordDocument;
import com.endpoint.frelog.domain.jpa.DataRecordEntity;
import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.repository.elasticsearch.DataRecordElasticsearchRepository;
import com.endpoint.frelog.repository.jpa.DataRecordJpaRepository;
import com.endpoint.frelog.service.TieredDataService;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.scheduling.annotation.Scheduled;
import org.springframework.stereotype.Component;
import org.springframework.transaction.annotation.Transactional;

import java.util.ArrayList;
import java.util.List;

@Slf4j
@Component
@RequiredArgsConstructor
public class TieredStorageScheduler {

    private final TieredDataService tieredDataService;
    private final DataRecordJpaRepository dataRecordJpaRepository;
    private final DataRecordElasticsearchRepository dataRecordElasticsearchRepository;
    private final ObjectMapper objectMapper;

    @Value("${app.tiered-storage.cold-threshold-score:2.0}")
    private double coldThresholdScore;

    @Value("${app.tiered-storage.batch-limit:50}")
    private int batchLimit;

    /**
     * 주기적으로 덜 사용된(Cold) 데이터를 탐색하여
     * MS SQL과 Elasticsearch에 영구 보관 후 Redis 캐시에서 퇴거(Evict)합니다.
     */
    @Scheduled(fixedDelayString = "${app.tiered-storage.scheduler.fixed-delay:10000}")
    public void offloadColdDataToPermanentStorage() {
        long currentCacheSize = tieredDataService.getCacheSize();
        if (currentCacheSize == 0) {
            return;
        }

        // 사용 빈도가 임계값 이하인 콜드 데이터 추출
        List<DataRecordDto> coldRecords = tieredDataService.identifyColdRecords(coldThresholdScore, batchLimit);
        if (coldRecords.isEmpty()) {
            log.debug("No cold records found. All items in Redis are frequently accessed (HOT).");
            return;
        }

        log.info("Found {} cold records to offload to permanent storage (MS SQL & ES)...", coldRecords.size());

        try {
            // 1. MS SQL 영구 저장 (JPA 일괄 저장)
            saveToMsSql(coldRecords);

            // 2. Elasticsearch 인덱싱 일괄 저장
            saveToElasticsearch(coldRecords);

            // 3. 영구 저장 성공 후, 덜 사용된 데이터를 Redis에서 퇴거(Evict)하여 메모리 확보
            List<String> evictedIds = coldRecords.stream().map(DataRecordDto::getId).toList();
            tieredDataService.evictFromRedis(evictedIds);

            log.info("Successfully offloaded {} cold records to permanent storage. Hot data remains in Redis.", coldRecords.size());
        } catch (Exception e) {
            log.error("Failed to offload cold records to permanent storage. Aborting eviction to prevent data loss.", e);
        }
    }

    @Transactional
    protected void saveToMsSql(List<DataRecordDto> dtoList) {
        List<DataRecordEntity> entities = new ArrayList<>();

        for (DataRecordDto dto : dtoList) {
            String metadataJson = null;
            if (dto.getMetadata() != null && !dto.getMetadata().isEmpty()) {
                try {
                    metadataJson = objectMapper.writeValueAsString(dto.getMetadata());
                } catch (JsonProcessingException ignored) {
                }
            }

            DataRecordEntity entity = DataRecordEntity.builder()
                    .id(dto.getId())
                    .title(dto.getTitle())
                    .content(dto.getContent())
                    .accessCount(dto.getAccessCount())
                    .createdAt(dto.getCreatedAt())
                    .lastAccessedAt(dto.getLastAccessedAt())
                    .metadata(metadataJson)
                    .build();

            entities.add(entity);
        }

        dataRecordJpaRepository.saveAll(entities);
    }

    protected void saveToElasticsearch(List<DataRecordDto> dtoList) {
        List<DataRecordDocument> documents = new ArrayList<>();

        for (DataRecordDto dto : dtoList) {
            DataRecordDocument doc = DataRecordDocument.builder()
                    .id(dto.getId())
                    .title(dto.getTitle())
                    .content(dto.getContent())
                    .accessCount(dto.getAccessCount())
                    .createdAt(dto.getCreatedAt())
                    .lastAccessedAt(dto.getLastAccessedAt())
                    .metadata(dto.getMetadata())
                    .build();

            documents.add(doc);
        }

        dataRecordElasticsearchRepository.saveAll(documents);
    }
}
