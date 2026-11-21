package com.endpoint.frelog.domain.auth.session;

import jakarta.persistence.LockModeType;
import org.springframework.data.jpa.repository.*;
import java.time.LocalDateTime;
import java.util.Optional;

public interface AuthSessionRepository extends JpaRepository<AuthSession,String> {
    @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select s from AuthSession s where s.refreshHash=:hash") Optional<AuthSession> lockByRefreshHash(String hash);
    @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select s from AuthSession s where s.previousHash=:hash") Optional<AuthSession> lockByPreviousHash(String hash);
    @Query("select count(s)>0 from AuthSession s where s.id=:id and s.userId=:userId and s.revokedAt is null and s.expiresAt>:now")
    boolean isActive(String id,Long userId,LocalDateTime now);
    @Modifying @Query("update AuthSession s set s.revokedAt=:now where s.userId=:userId and s.revokedAt is null and (:keep is null or s.id<>:keep)")
    int revokeAll(Long userId,String keep,LocalDateTime now);
    @Modifying @Query("delete from AuthSession s where s.expiresAt<:cutoff or s.revokedAt<:cutoff") void deleteStale(LocalDateTime cutoff);
}
