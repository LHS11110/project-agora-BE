package com.endpoint.frelog.repository.jpa;

import com.endpoint.frelog.domain.jpa.LogEntity;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface LogJpaRepository extends JpaRepository<LogEntity, Long> {
}
