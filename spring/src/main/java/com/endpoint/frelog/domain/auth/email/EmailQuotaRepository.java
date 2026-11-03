package com.endpoint.frelog.domain.auth.email;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.time.LocalDateTime;
import java.util.Optional;
public interface EmailQuotaRepository extends JpaRepository<EmailQuota,String> {
 @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select q from EmailQuota q where q.id=:id") Optional<EmailQuota> lock(String id);
 @Modifying @Query("delete from EmailQuota q where q.windowStart<:cutoff") void cleanup(LocalDateTime cutoff);
}
