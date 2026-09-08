package com.endpoint.frelog.controller;

import com.endpoint.frelog.dto.DataRecordDto;
import com.endpoint.frelog.service.TieredDataService;
import lombok.RequiredArgsConstructor;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.*;

import java.util.List;
import java.util.Map;

@RestController
@RequestMapping("/api/v1/data")
@RequiredArgsConstructor
public class DataRecordController {

    private final TieredDataService tieredDataService;

    /**
     * 임시 POST 엔드포인트: 데이터 등록 및 Redis 캐시에 저장 (Hot 상태로 시작)
     */
    @PostMapping
    public ResponseEntity<DataRecordDto> createData(@RequestBody DataRecordDto dto) {
        DataRecordDto saved = tieredDataService.save(dto);
        return ResponseEntity.status(HttpStatus.CREATED).body(saved);
    }

    /**
     * 데이터 조회:
     * 호출 시마다 접근 빈도(score)가 증가하여 Redis Hot 캐시에 유지됩니다.
     * 이미 콜드로 퇴거된 데이터인 경우 MS SQL에서 복원 후 Redis로 자동 재승격(Promotion)됩니다.
     */
    @GetMapping("/{id}")
    public ResponseEntity<DataRecordDto> getData(@PathVariable String id) {
        return tieredDataService.get(id)
                .map(ResponseEntity::ok)
                .orElseGet(() -> ResponseEntity.notFound().build());
    }

    /**
     * Redis 캐시에 남아있는 가장 빈번히 사용된 Hot 데이터 상위 랭킹 조회
     */
    @GetMapping("/hot-ranking")
    public ResponseEntity<List<Map<String, Object>>> getHotRanking(
            @RequestParam(defaultValue = "10") int topN) {
        List<Map<String, Object>> ranking = tieredDataService.getHotRanking(topN);
        return ResponseEntity.ok(ranking);
    }

    /**
     * 현재 Redis 캐시 상태 통계 조회
     */
    @GetMapping("/stats")
    public ResponseEntity<Map<String, Object>> getStats() {
        long cacheSize = tieredDataService.getCacheSize();
        return ResponseEntity.ok(Map.of(
                "cachedRecordsCount", cacheSize,
                "strategy", "LFU-Tiered-Storage",
                "hotStorage", "Redis",
                "coldStorage", "MS SQL Server & Elasticsearch"
        ));
    }
}
