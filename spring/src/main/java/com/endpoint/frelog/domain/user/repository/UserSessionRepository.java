package com.endpoint.frelog.domain.user.repository;

import com.endpoint.frelog.domain.user.entity.UserSession;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.data.jpa.repository.Lock;
import org.springframework.data.jpa.repository.Modifying;
import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;
import org.springframework.stereotype.Repository;
import jakarta.persistence.LockModeType;
import java.util.Optional;

@Repository
public interface UserSessionRepository extends JpaRepository<UserSession, Long> {
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("SELECT s FROM UserSession s WHERE s.userId = :userId")
    Optional<UserSession> findByIdWithPessimisticLock(@Param("userId") Long userId);

    boolean existsByCanvas_CanvasIdAndIsAccessedTrue(Integer canvasId);

    boolean existsByCppServer_ServerId(Integer serverId);

    @Modifying(clearAutomatically = true, flushAutomatically = true)
    @Query("UPDATE UserSession s SET s.canvas = null, s.cppServer = null " +
            "WHERE s.canvas.canvasId = :canvasId AND s.isAccessed = false")
    int clearInactiveCanvasReferences(@Param("canvasId") Integer canvasId);
}
