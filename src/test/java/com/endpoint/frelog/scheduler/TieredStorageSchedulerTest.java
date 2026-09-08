package com.endpoint.frelog.scheduler;

import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.repository.elasticsearch.DataRecordElasticsearchRepository;
import com.endpoint.frelog.repository.jpa.DataRecordJpaRepository;
import com.endpoint.frelog.service.TieredDataService;
import com.fasterxml.jackson.databind.ObjectMapper;
import org.junit.jupiter.api.BeforeEach;
import org.junit.jupiter.api.DisplayName;
import org.junit.jupiter.api.Test;
import org.junit.jupiter.api.extension.ExtendWith;
import org.mockito.InjectMocks;
import org.mockito.Mock;
import org.mockito.Spy;
import org.mockito.junit.jupiter.MockitoExtension;
import org.springframework.test.util.ReflectionTestUtils;

import java.time.LocalDateTime;
import java.util.Collections;
import java.util.List;

import static org.mockito.ArgumentMatchers.anyList;
import static org.mockito.Mockito.*;

@ExtendWith(MockitoExtension.class)
class TieredStorageSchedulerTest {

    @Mock
    private TieredDataService tieredDataService;

    @Mock
    private DataRecordJpaRepository dataRecordJpaRepository;

    @Mock
    private DataRecordElasticsearchRepository dataRecordElasticsearchRepository;

    @Spy
    private ObjectMapper objectMapper = new ObjectMapper();

    @InjectMocks
    private TieredStorageScheduler scheduler;

    @BeforeEach
    void setUp() {
        ReflectionTestUtils.setField(scheduler, "coldThresholdScore", 2.0);
        ReflectionTestUtils.setField(scheduler, "batchLimit", 50);
    }

    @Test
    @DisplayName("콜드 데이터가 존재할 경우 MS SQL과 ES에 영구 저장 후 Redis에서 퇴거한다")
    void offloadColdData_Success() {
        // given
        when(tieredDataService.getCacheSize()).thenReturn(10L);

        List<DataRecordDto> coldList = List.of(
                DataRecordDto.builder()
                        .id("cold-1")
                        .title("Cold Title")
                        .content("Cold Content")
                        .accessCount(1L)
                        .createdAt(LocalDateTime.now())
                        .lastAccessedAt(LocalDateTime.now())
                        .build()
        );
        when(tieredDataService.identifyColdRecords(2.0, 50)).thenReturn(coldList);

        // when
        scheduler.offloadColdDataToPermanentStorage();

        // then
        verify(dataRecordJpaRepository, times(1)).saveAll(anyList());
        verify(dataRecordElasticsearchRepository, times(1)).saveAll(anyList());
        verify(tieredDataService, times(1)).evictFromRedis(List.of("cold-1"));
    }

    @Test
    @DisplayName("콜드 데이터가 없으면 영구 저장 및 퇴거를 수행하지 않는다")
    void offloadColdData_WhenNoColdRecords_DoNothing() {
        // given
        when(tieredDataService.getCacheSize()).thenReturn(10L);
        when(tieredDataService.identifyColdRecords(2.0, 50)).thenReturn(Collections.emptyList());

        // when
        scheduler.offloadColdDataToPermanentStorage();

        // then
        verify(dataRecordJpaRepository, never()).saveAll(anyList());
        verify(dataRecordElasticsearchRepository, never()).saveAll(anyList());
        verify(tieredDataService, never()).evictFromRedis(anyList());
    }
}
