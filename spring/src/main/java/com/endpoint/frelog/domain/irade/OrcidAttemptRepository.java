package com.endpoint.frelog.domain.irade;

import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.time.LocalDateTime;
import java.util.Optional;

public interface OrcidAttemptRepository extends JpaRepository<OrcidAttempt,String> {
    @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select a from OrcidAttempt a where a.id=:id") Optional<OrcidAttempt> lock(String id);
    @Modifying @Query("delete from OrcidAttempt a where a.expiresAt<:before") void deleteExpired(LocalDateTime before);
}
