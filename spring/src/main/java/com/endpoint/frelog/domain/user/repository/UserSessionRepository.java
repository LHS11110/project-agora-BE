package com.endpoint.frelog.domain.user.repository;

import com.endpoint.frelog.domain.user.entity.UserSession;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;
import jakarta.persistence.LockModeType;
import java.util.Optional;
import java.util.List;

@Repository
public interface UserSessionRepository extends JpaRepository<UserSession, Long> {
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("SELECT s FROM UserSession s WHERE s.userId = :userId")
    Optional<UserSession> findByIdWithPessimisticLock(@Param("userId") Long userId);

    @Query("SELECT COUNT(DISTINCT s.canvas.canvasId) FROM UserSession s " +
            "WHERE s.isAccessed = true AND s.cppServer.serverId = :serverId")
    long countActiveCanvasesByCppServerId(@Param("serverId") Integer serverId);

    boolean existsByCanvas_CanvasIdAndIsAccessedTrue(Integer canvasId);

    List<UserSession> findByIsAccessedTrue();

    boolean existsByCppServer_ServerId(Integer serverId);

}
