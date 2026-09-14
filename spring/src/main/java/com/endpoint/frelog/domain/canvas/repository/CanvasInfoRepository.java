package com.endpoint.frelog.domain.canvas.repository;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Query;
import org.springframework.stereotype.Repository;

import java.util.Optional;

@Repository
public interface CanvasInfoRepository extends JpaRepository<CanvasInfo, Integer> {

    @Query("SELECT COALESCE(MAX(c.canvasId), 0) FROM CanvasInfo c")
    Integer findMaxCanvasId();

    long countByRedisIpAndRedisPort(String redisIp, String redisPort);

    long countByServerIpAndServerPort(String serverIp, String serverPort);

    boolean existsByRedisIpAndRedisPortAndIsCachedTrue(String redisIp, String redisPort);

    boolean existsByServerIpAndServerPortAndIsCachedTrue(String serverIp, String serverPort);
}
