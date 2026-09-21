package com.endpoint.frelog.domain.canvas.repository;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import jakarta.persistence.LockModeType;
import java.util.Optional;

@Repository
public interface CanvasInfoRepository extends JpaRepository<CanvasInfo, Integer> {

    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("SELECT c FROM CanvasInfo c WHERE c.canvasId = :canvasId")
    Optional<CanvasInfo> findByIdWithPessimisticLock(@Param("canvasId") Integer canvasId);

    @Query("SELECT COALESCE(MAX(c.canvasId), 0) FROM CanvasInfo c")
    Integer findMaxCanvasId();

    long countByRedisInfo_RedisIpAndRedisInfo_RedisPort(String redisIp, String redisPort);

    long countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(String redisIp, String redisPort);

    long countByCppServer_ServerIpAndCppServer_ServerPort(String serverIp, String serverPort);

    boolean existsByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(String redisIp, String redisPort);

    boolean existsByCppServer_ServerIpAndCppServer_ServerPortAndIsCachedTrue(String serverIp, String serverPort);
}
