package com.endpoint.frelog.domain.user.repository;

import com.endpoint.frelog.domain.user.entity.User;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import org.springframework.data.jpa.repository.Query;
import org.springframework.data.repository.query.Param;

import java.util.Optional;

@Repository
public interface UserRepository extends JpaRepository<User, Long> {

    Optional<User> findByEmail(String email);

    boolean existsByEmail(String email);

    @Query("SELECT COALESCE(MAX(u.tagNumber), 0) FROM User u WHERE u.nickname = :nickname")
    Integer findMaxTagNumberByNickname(@Param("nickname") String nickname);

    Optional<User> findByNicknameAndTagNumber(String nickname, Integer tagNumber);
}
