package com.endpoint.frelog.domain.loadbalancer.repository;

import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface RedisInfoRepository extends JpaRepository<RedisInfo, Long> {

    List<RedisInfo> findByIsActivatedTrue();

    Optional<RedisInfo> findByRedisIpAndRedisPort(String redisIp, String redisPort);

    boolean existsByRedisIpAndRedisPort(String redisIp, String redisPort);
}
