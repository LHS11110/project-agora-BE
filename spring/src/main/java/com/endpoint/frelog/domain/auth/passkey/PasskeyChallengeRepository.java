package com.endpoint.frelog.domain.auth.passkey;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.time.LocalDateTime;
import java.util.Optional;
public interface PasskeyChallengeRepository extends JpaRepository<PasskeyChallenge,String> {
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("select c from PasskeyChallenge c where c.id=:id") Optional<PasskeyChallenge> lock(String id);
    long countByClientIpAndCreatedAtAfter(String clientIp,LocalDateTime since);
    @Modifying @Query("delete from PasskeyChallenge c where c.expiresAt<:cutoff") void deleteExpired(LocalDateTime cutoff);
}
