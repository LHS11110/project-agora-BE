package com.endpoint.frelog.domain.user.repository;

import com.endpoint.frelog.domain.user.entity.User;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.Optional;

@Repository
public interface UserRepository extends JpaRepository<User, Long> {

    @org.springframework.data.jpa.repository.Lock(jakarta.persistence.LockModeType.PESSIMISTIC_WRITE)
    @Query("select u from User u where u.userId=:id")
    Optional<User> lockById(Long id);

    Optional<User> findByEmail(String email);

    @org.springframework.data.jpa.repository.Lock(jakarta.persistence.LockModeType.PESSIMISTIC_WRITE)
    Optional<User> findByOrcidAndOrcidIssuer(String orcid,String orcidIssuer);

    @org.springframework.data.jpa.repository.Lock(jakarta.persistence.LockModeType.PESSIMISTIC_WRITE)
    Optional<User> findByOauthProviderAndOauthId(String provider, String subject);

    java.util.List<User> findByOauthProviderAndOauthIdStartingWith(String provider, String prefix);

    boolean existsByEmail(String email);

    @Query(value = "SELECT COALESCE(MAX(tag_number), 0) FROM users WITH (UPDLOCK, HOLDLOCK) WHERE nickname = :nickname",
            nativeQuery = true)
    Integer findMaxTagNumberByNicknameForUpdate(@Param("nickname") String nickname);

    Optional<User> findByNicknameAndTagNumber(String nickname, Integer tagNumber);
}
