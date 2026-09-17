package com.endpoint.frelog.domain.loadbalancer.controller;

import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.DatabaseAddressResponse;
import com.endpoint.frelog.domain.loadbalancer.service.LoadBalancerService;
import jakarta.validation.Valid;
import org.springframework.http.HttpStatus;
import org.springframework.http.ResponseEntity;
import org.springframework.web.bind.annotation.DeleteMapping;
import org.springframework.web.bind.annotation.GetMapping;
import org.springframework.web.bind.annotation.PathVariable;
import org.springframework.web.bind.annotation.PostMapping;
import org.springframework.web.bind.annotation.PutMapping;
import org.springframework.web.bind.annotation.RequestBody;
import org.springframework.web.bind.annotation.RequestMapping;
import org.springframework.web.bind.annotation.RestController;

import java.util.List;

@RestController
public class LoadBalancerController {

    private final LoadBalancerService loadBalancerService;

    public LoadBalancerController(LoadBalancerService loadBalancerService) {
        this.loadBalancerService = loadBalancerService;
    }

    // =========================================================================
    // 로드 밸런서 할당 API (Power of Two Choices)
    // =========================================================================

    @PostMapping("/api/load-balancer/allocate/server")
    public ResponseEntity<AllocateServerResponse> allocateServerPost() {
        AllocateServerResponse response = loadBalancerService.allocateServer();
        return ResponseEntity.ok(response);
    }

    @GetMapping("/api/load-balancer/allocate/server")
    public ResponseEntity<AllocateServerResponse> allocateServerGet() {
        AllocateServerResponse response = loadBalancerService.allocateServer();
        return ResponseEntity.ok(response);
    }

    @PostMapping("/api/load-balancer/allocate/redis")
    public ResponseEntity<AllocateRedisResponse> allocateRedisPost() {
        AllocateRedisResponse response = loadBalancerService.allocateRedis();
        return ResponseEntity.ok(response);
    }

    @GetMapping("/api/load-balancer/allocate/redis")
    public ResponseEntity<AllocateRedisResponse> allocateRedisGet() {
        AllocateRedisResponse response = loadBalancerService.allocateRedis();
        return ResponseEntity.ok(response);
    }

    // =========================================================================
    // Database 주소 조회 및 할당 API
    // =========================================================================

    @GetMapping("/api/load-balancer/database")
    public ResponseEntity<DatabaseAddressResponse> getDatabaseAddress() {
        return ResponseEntity.ok(loadBalancerService.getDatabaseAddress());
    }

    @PostMapping("/api/load-balancer/allocate/database")
    public ResponseEntity<DatabaseAddressResponse> allocateDatabasePost() {
        return ResponseEntity.ok(loadBalancerService.getDatabaseAddress());
    }

    @GetMapping("/api/database/address")
    public ResponseEntity<DatabaseAddressResponse> getDatabaseAddressAlias() {
        return ResponseEntity.ok(loadBalancerService.getDatabaseAddress());
    }

}
