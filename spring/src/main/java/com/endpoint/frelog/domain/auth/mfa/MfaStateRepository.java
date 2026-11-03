package com.endpoint.frelog.domain.auth.mfa;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.util.Optional;
public interface MfaStateRepository extends JpaRepository<MfaState,Long> {
    @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select s from MfaState s where s.userId=:id") Optional<MfaState> lock(Long id);
}
