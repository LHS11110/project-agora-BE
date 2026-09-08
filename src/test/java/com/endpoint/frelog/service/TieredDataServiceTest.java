package com.endpoint.frelog.service;

import com.endpoint.frelog.domain.jpa.DataRecordEntity;
import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.repository.jpa.DataRecordJpaRepository;
import com.fasterxml.jackson.databind.ObjectMapper;
import com.fasterxml.jackson.datatype.jsr310.JavaTimeModule;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.data.redis.core.HashOperations;
import org.springframework.data.redis.core.RedisTemplate;
import org.springframework.data.redis.core.ZSetOperations;

import java.time.LocalDateTime;
import java.util.List;
import java.util.Optional;
import java.util.Set;

import static org.assertj.core.api.Assertions.assertThat;
import static org.mockito.ArgumentMatchers.any;
import static org.mockito.ArgumentMatchers.eq;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class TieredDataServiceTest {

    @Mock
    private RedisTemplate<String, Object> redisTemplate;

    @Mock
    private HashOperations<String, Object, Object> hashOperations;

    @Mock
    private ZSetOperations<String, Object> zSetOperations;

    @Mock
    private DataRecordJpaRepository dataRecordJpaRepository;

    @Spy
    private ObjectMapper objectMapper = new ObjectMapper().registerModule(new JavaTimeModule());

    @InjectMocks
    private TieredDataService tieredDataService;

    @BeforeEach
    void setUp() {
        lenient().doReturn(hashOperations).when(redisTemplate).opsForHash();
        lenient().doReturn(zSetOperations).when(redisTemplate).opsForZSet();
    }

    @Test
    @DisplayName("신규 데이터 저장 시 Redis Hash와 ZSet(score=5)에 저장된다")
    void save_Success() {
        // given
        DataRecordDto dto = DataRecordDto.builder()
                .title("Hot Item")
                .content("Frequently accessed data")
                .build();

        // when
        DataRecordDto saved = tieredDataService.save(dto);

        // then
        assertThat(saved.getId()).isNotNull();
        assertThat(saved.getAccessCount()).isEqualTo(5L);
        verify(hashOperations, times(1)).put(eq(TieredDataService.CACHE_RECORDS_KEY), eq(saved.getId()), any());
        verify(zSetOperations, times(1)).add(eq(TieredDataService.CACHE_ACTIVITY_KEY), eq(saved.getId()), eq(5.0));
    }

    @Test
    @DisplayName("캐시 Hit 시 Redis ZSet 점수가 1 증가하여 Hot Data로 유지된다")
    void get_CacheHit_IncrementsScore() throws Exception {
        // given
        String id = "hot-item-123";
        DataRecordDto cachedDto = DataRecordDto.builder()
                .id(id)
                .title("Hot Item")
                .accessCount(5L)
                .build();

        String json = objectMapper.writeValueAsString(cachedDto);
        doReturn(json).when(hashOperations).get(TieredDataService.CACHE_RECORDS_KEY, id);
        doReturn(6.0).when(zSetOperations).incrementScore(TieredDataService.CACHE_ACTIVITY_KEY, id, 1.0);

        // when
        Optional<DataRecordDto> result = tieredDataService.get(id);

        // then
        assertThat(result).isPresent();
        assertThat(result.get().getAccessCount()).isEqualTo(6L);
        verify(zSetOperations, times(1)).incrementScore(TieredDataService.CACHE_ACTIVITY_KEY, id, 1.0);
        verify(dataRecordJpaRepository, never()).findById(any());
    }

    @Test
    @DisplayName("캐시 Miss 시 MS SQL에서 조회 후 Redis로 재승격(Promotion)된다")
    void get_CacheMiss_PromotesFromMsSql() {
        // given
        String id = "cold-item-999";
        doReturn(null).when(hashOperations).get(TieredDataService.CACHE_RECORDS_KEY, id);

        DataRecordEntity entity = DataRecordEntity.builder()
                .id(id)
                .title("Cold Item From DB")
                .content("Stored in MSSQL")
                .accessCount(2L)
                .createdAt(LocalDateTime.now())
                .lastAccessedAt(LocalDateTime.now())
                .build();
        doReturn(Optional.of(entity)).when(dataRecordJpaRepository).findById(id);

        // when
        Optional<DataRecordDto> result = tieredDataService.get(id);

        // then
        assertThat(result).isPresent();
        assertThat(result.get().getTitle()).isEqualTo("Cold Item From DB");
        assertThat(result.get().getAccessCount()).isEqualTo(3L);
        // Redis로 재승격 저장 호출 검증
        verify(hashOperations, times(1)).put(eq(TieredDataService.CACHE_RECORDS_KEY), eq(id), any());
        verify(zSetOperations, times(1)).add(eq(TieredDataService.CACHE_ACTIVITY_KEY), eq(id), eq(3.0));
    }

    @Test
    @DisplayName("콜드 데이터 식별 시 ZSet에서 임계치 이하 항목들을 반환한다")
    void identifyColdRecords_Success() throws Exception {
        // given
        String coldId = "cold-1";
        Set<Object> coldSet = Set.of(coldId);
        doReturn(coldSet).when(zSetOperations).rangeByScore(TieredDataService.CACHE_ACTIVITY_KEY, 0.0, 2.0, 0, 10);

        DataRecordDto coldDto = DataRecordDto.builder().id(coldId).title("Cold 1").build();
        String json = objectMapper.writeValueAsString(coldDto);
        doReturn(json).when(hashOperations).get(TieredDataService.CACHE_RECORDS_KEY, coldId);

        // when
        List<DataRecordDto> coldRecords = tieredDataService.identifyColdRecords(2.0, 10);

        // then
        assertThat(coldRecords).hasSize(1);
        assertThat(coldRecords.get(0).getId()).isEqualTo(coldId);
    }

    @Test
    @DisplayName("evictFromRedis 호출 시 영구 저장된 콜드 데이터가 Redis Hash 및 ZSet에서 제거된다")
    void evictFromRedis_Success() {
        // given
        List<String> ids = List.of("cold-1", "cold-2");

        // when
        tieredDataService.evictFromRedis(ids);

        // then
        verify(hashOperations, times(1)).delete(eq(TieredDataService.CACHE_RECORDS_KEY), any(Object[].class));
        verify(zSetOperations, times(1)).remove(eq(TieredDataService.CACHE_ACTIVITY_KEY), any(Object[].class));
    }
}
