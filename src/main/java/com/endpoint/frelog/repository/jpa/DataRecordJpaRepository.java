package com.endpoint.frelog.repository.jpa;

import com.endpoint.frelog.domain.jpa.DataRecordEntity;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

@Repository
public interface DataRecordJpaRepository extends JpaRepository<DataRecordEntity, String> {
}
