package com.endpoint.frelog.domain.auth.mfa;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.util.Optional;
import java.time.LocalDateTime;
public interface MfaChallengeRepository extends JpaRepository<MfaChallenge,String> {
    @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select c from MfaChallenge c where c.id=:id") Optional<MfaChallenge> lock(String id);
    @Modifying @Query("delete from MfaChallenge c where c.expiresAt<:cutoff") void deleteExpired(LocalDateTime cutoff);
}
