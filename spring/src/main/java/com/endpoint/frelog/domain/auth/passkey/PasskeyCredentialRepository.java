package com.endpoint.frelog.domain.auth.passkey;
import org.springframework.data.jpa.repository.*;
import jakarta.persistence.LockModeType;
import java.util.*;
public interface PasskeyCredentialRepository extends JpaRepository<PasskeyCredential,String> {
    List<PasskeyCredential> findByUserIdOrderByCreatedAtAsc(Long userId);
    Optional<PasskeyCredential> findByCredentialHash(String hash);
    Optional<PasskeyCredential> findFirstByUserHandle(String handle);
    @Lock(LockModeType.PESSIMISTIC_WRITE)
    @Query("select p from PasskeyCredential p where p.credentialHash=:hash") Optional<PasskeyCredential> lockCredential(String hash);
}
