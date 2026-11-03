package com.endpoint.frelog.domain.auth.email;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.time.LocalDateTime;
import java.util.Optional;
public interface EmailProofRepository extends JpaRepository<EmailProof,String> {
 @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select p from EmailProof p where p.email=:email") Optional<EmailProof> lock(String email);
 @Modifying @Query("delete from EmailProof p where p.windowStart<:cutoff and p.expiresAt<:cutoff and (p.grantExpiresAt is null or p.grantExpiresAt<:cutoff)") void cleanup(LocalDateTime cutoff);
}
