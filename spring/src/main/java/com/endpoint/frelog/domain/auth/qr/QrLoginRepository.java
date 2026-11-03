package com.endpoint.frelog.domain.auth.qr;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.time.LocalDateTime;
import java.util.Optional;
public interface QrLoginRepository extends JpaRepository<QrLogin,String>{
 @Lock(LockModeType.PESSIMISTIC_WRITE) @Query("select q from QrLogin q where q.id=:id") Optional<QrLogin> lock(String id);
 long countByRequesterIpAndCreatedAtAfter(String ip,LocalDateTime since);
 @Modifying @Query("delete from QrLogin q where q.expiresAt<:cutoff") void cleanup(LocalDateTime cutoff);
}
