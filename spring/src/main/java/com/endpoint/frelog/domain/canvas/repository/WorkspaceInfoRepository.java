package com.endpoint.frelog.domain.canvas.repository;

import com.endpoint.frelog.domain.canvas.entity.WorkspaceInfo;
import jakarta.persistence.LockModeType;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;

import java.time.LocalDateTime;
import java.util.Optional;

@Repository
public interface WorkspaceInfoRepository extends JpaRepository<WorkspaceInfo, Integer> {

    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("SELECT w FROM WorkspaceInfo w WHERE w.workspaceId = :workspaceId")
    Optional<WorkspaceInfo> findByIdWithPessimisticLock(@Param("workspaceId") Integer workspaceId);

    @Query("SELECT s.serverId FROM WorkspaceInfo w JOIN w.cppServer s WHERE w.workspaceId = :workspaceId " +
            "AND w.isCached = true AND s.isActivated = true AND s.lastHeartbeatAt > :cutoff")
    Optional<Integer> findActiveCppServerId(@Param("workspaceId") Integer workspaceId,
                                            @Param("cutoff") LocalDateTime cutoff);

    long countByRedisInfo_RedisIpAndRedisInfo_RedisPort(String redisIp, String redisPort);

    long countByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(String redisIp, String redisPort);

    long countByCppServer_ServerIpAndCppServer_ServerPort(String serverIp, String serverPort);

    boolean existsByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(String redisIp, String redisPort);

    boolean existsByCppServer_ServerIpAndCppServer_ServerPortAndIsCachedTrue(String serverIp, String serverPort);
}
