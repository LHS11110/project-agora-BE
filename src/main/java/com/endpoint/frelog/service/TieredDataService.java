package com.endpoint.frelog.service;

import com.endpoint.frelog.domain.jpa.DataRecordEntity;
import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.repository.jpa.DataRecordJpaRepository;
import com.fasterxml.jackson.core.JsonProcessingException;
import com.fasterxml.jackson.databind.ObjectMapper;
import lombok.RequiredArgsConstructor;
import lombok.extern.slf4j.Slf4j;
import org.springframework.data.redis.core.RedisTemplate;
import org.springframework.data.redis.core.ZSetOperations;
import org.springframework.stereotype.Service;

import java.time.LocalDateTime;
import java.util.*;

@Slf4j
@Service
@RequiredArgsConstructor
public class TieredDataService {

    public static final String CACHE_RECORDS_KEY = "cache:data:records";
    public static final String CACHE_ACTIVITY_KEY = "cache:data:activity";

    private final RedisTemplate<String, Object> redisTemplate;
    private final DataRecordJpaRepository dataRecordJpaRepository;
    private final ObjectMapper objectMapper;

    @org.springframework.beans.factory.annotation.Value("${app.tiered-storage.initial-score:5.0}")
    private double initialScore = 5.0;

    /**
     * 신규 데이터 등록 및 Redis 캐시 저장
     * 초기 접근 빈도(score=5.0)를 부여하여 Redis Hot 영역에 저장합니다.
     */
    public DataRecordDto save(DataRecordDto dto) {
        if (dto.getId() == null || dto.getId().isBlank()) {
            dto.setId(UUID.randomUUID().toString());
        }

        if (dto.getAccessCount() == null || dto.getAccessCount() <= 0) {
            dto.setAccessCount((long) initialScore);
        }

        LocalDateTime now = LocalDateTime.now();
        if (dto.getCreatedAt() == null) {
            dto.setCreatedAt(now);
        }
        dto.setLastAccessedAt(now);

        // 1. Redis Hash에 본문 저장
        saveDtoToHash(dto);

        // 2. Redis Sorted Set에 빈도수(score) 기록
        redisTemplate.opsForZSet().add(CACHE_ACTIVITY_KEY, dto.getId(), dto.getAccessCount().doubleValue());

        log.debug("Saved record [{}] into Redis cache with initial score {}", dto.getId(), dto.getAccessCount());
        return dto;
    }

    /**
     * 데이터 조회:
     * 1) Redis 캐시 Hit 시: 접근 빈도(score)를 1 증가시켜 Hot Data로 유지
     * 2) Redis 캐시 Miss 시: MS SQL 영구 저장소에서 조회 후 Redis로 승격(Promotion)
     */
    public Optional<DataRecordDto> get(String id) {
        // 1. Redis Hash 조회
        Object rawJson = redisTemplate.opsForHash().get(CACHE_RECORDS_KEY, id);
        if (rawJson != null) {
            DataRecordDto dto = deserialize(rawJson.toString());
            if (dto != null) {
                // 접근 빈도 점수 1 증가 (Hot 유지)
                Double newScore = redisTemplate.opsForZSet().incrementScore(CACHE_ACTIVITY_KEY, id, 1.0);
                long updatedCount = newScore != null ? newScore.longValue() : dto.getAccessCount() + 1;

                dto.setAccessCount(updatedCount);
                dto.setLastAccessedAt(LocalDateTime.now());
                saveDtoToHash(dto);

                log.debug("Cache HIT for record [{}], updated accessCount: {}", id, updatedCount);
                return Optional.of(dto);
            }
        }

        // 2. Redis Miss ➔ MS SQL 영구 저장소 확인 (Cache-Aside & Promotion)
        log.debug("Cache MISS for record [{}], querying MS SQL permanent storage...", id);
        Optional<DataRecordEntity> entityOpt = dataRecordJpaRepository.findById(id);
        if (entityOpt.isPresent()) {
            DataRecordEntity entity = entityOpt.get();
            DataRecordDto promotedDto = convertEntityToDto(entity);

            // 다시 사용되었으므로 Redis로 재승격 (Promotion)
            promotedDto.setAccessCount(promotedDto.getAccessCount() + 1);
            promotedDto.setLastAccessedAt(LocalDateTime.now());
            save(promotedDto);

            log.info("Promoted cold record [{}] from MS SQL back to Redis cache (Score: {})", id, promotedDto.getAccessCount());
            return Optional.of(promotedDto);
        }

        return Optional.empty();
    }

    /**
     * 덜 사용된(Cold) 데이터 목록 식별:
     * Sorted Set에서 점수가 maxScore 이하인 하위 항목들을 조회
     */
    public List<DataRecordDto> identifyColdRecords(double maxScore, int limit) {
        Set<Object> coldIds = redisTemplate.opsForZSet().rangeByScore(CACHE_ACTIVITY_KEY, 0, maxScore, 0, limit);
        if (coldIds == null || coldIds.isEmpty()) {
            return Collections.emptyList();
        }

        List<DataRecordDto> coldList = new ArrayList<>();
        for (Object rawId : coldIds) {
            String id = String.valueOf(rawId);
            Object raw = redisTemplate.opsForHash().get(CACHE_RECORDS_KEY, id);
            if (raw != null) {
                DataRecordDto dto = deserialize(raw.toString());
                if (dto != null) {
                    coldList.add(dto);
                }
            }
        }
        return coldList;
    }

    /**
     * 영구 저장소에 저장 완료된 콜드 데이터를 Redis에서 제거 (Eviction)
     * 이를 통해 자주 쓰이는 Hot 데이터만 Redis 메모리에 남깁니다.
     */
    public void evictFromRedis(List<String> ids) {
        if (ids == null || ids.isEmpty()) {
            return;
        }

        Object[] idArray = ids.toArray();
        redisTemplate.opsForHash().delete(CACHE_RECORDS_KEY, idArray);
        redisTemplate.opsForZSet().remove(CACHE_ACTIVITY_KEY, idArray);

        log.info("Evicted {} cold records from Redis cache to save memory: {}", ids.size(), ids);
    }

    /**
     * 안 쓰이는 데이터 점수 감쇠 (Score Decay / Aging):
     * 주기적으로 호출되어 Redis ZSet 내 모든 데이터의 점수에 decayFactor(예: 0.9)를 곱합니다.
     * 새로운 조회(Hit)가 없는 데이터는 점수가 점진적으로 하락하여 결국 콜드 임계치 이하로 떨어집니다.
     */
    public void decayScores(double decayFactor) {
        String luaScript =
                "local key = KEYS[1]\n" +
                "local factor = tonumber(ARGV[1])\n" +
                "local items = redis.call('ZRANGE', key, 0, -1, 'WITHSCORES')\n" +
                "for i = 1, #items, 2 do\n" +
                "    local member = items[i]\n" +
                "    local score = tonumber(items[i+1])\n" +
                "    local newScore = score * factor\n" +
                "    redis.call('ZADD', key, newScore, member)\n" +
                "end\n" +
                "return #items / 2";

        try {
            org.springframework.data.redis.core.script.DefaultRedisScript<Long> script =
                    new org.springframework.data.redis.core.script.DefaultRedisScript<>(luaScript, Long.class);
            redisTemplate.execute(script, Collections.singletonList(CACHE_ACTIVITY_KEY), String.valueOf(decayFactor));
            log.debug("Applied score decay with factor {} on key {}", decayFactor, CACHE_ACTIVITY_KEY);
        } catch (Exception e) {
            log.warn("Failed to execute score decay script: {}", e.getMessage());
        }
    }

    /**
     * 현재 Redis 캐시에서 가장 자주 사용된 Hot 데이터 상위 랭킹 조회
     */
    public List<Map<String, Object>> getHotRanking(int topN) {
        Set<ZSetOperations.TypedTuple<Object>> topTuples = redisTemplate.opsForZSet()
                .reverseRangeWithScores(CACHE_ACTIVITY_KEY, 0, Math.max(0, topN - 1));

        if (topTuples == null || topTuples.isEmpty()) {
            return Collections.emptyList();
        }

        List<Map<String, Object>> ranking = new ArrayList<>();
        for (ZSetOperations.TypedTuple<Object> tuple : topTuples) {
            String id = String.valueOf(tuple.getValue());
            Double score = tuple.getScore();
            Object raw = redisTemplate.opsForHash().get(CACHE_RECORDS_KEY, id);

            Map<String, Object> entry = new LinkedHashMap<>();
            entry.put("id", id);
            entry.put("accessScore", score);
            if (raw != null) {
                entry.put("record", deserialize(raw.toString()));
            }
            ranking.add(entry);
        }
        return ranking;
    }

    /**
     * 현재 Redis 캐시에 보관 중인 전체 레코드 수
     */
    public long getCacheSize() {
        Long size = redisTemplate.opsForHash().size(CACHE_RECORDS_KEY);
        return size != null ? size : 0L;
    }

    private void saveDtoToHash(DataRecordDto dto) {
        try {
            String json = objectMapper.writeValueAsString(dto);
            redisTemplate.opsForHash().put(CACHE_RECORDS_KEY, dto.getId(), json);
        } catch (JsonProcessingException e) {
            log.error("Failed to serialize DataRecordDto: {}", dto.getId(), e);
        }
    }

    private DataRecordDto deserialize(String json) {
        try {
            return objectMapper.readValue(json, DataRecordDto.class);
        } catch (Exception e) {
            log.error("Failed to deserialize DataRecordDto JSON: {}", json, e);
            return null;
        }
    }

    @SuppressWarnings("unchecked")
    private DataRecordDto convertEntityToDto(DataRecordEntity entity) {
        Map<String, Object> metadata = null;
        if (entity.getMetadata() != null && !entity.getMetadata().isBlank()) {
            try {
                metadata = objectMapper.readValue(entity.getMetadata(), Map.class);
            } catch (Exception ignored) {
            }
        }

        return DataRecordDto.builder()
                .id(entity.getId())
                .title(entity.getTitle())
                .content(entity.getContent())
                .accessCount(entity.getAccessCount())
                .createdAt(entity.getCreatedAt())
                .lastAccessedAt(entity.getLastAccessedAt())
                .metadata(metadata)
                .build();
    }
}
