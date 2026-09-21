package com.endpoint.frelog.domain.loadbalancer.repository;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;
import java.time.LocalDateTime;

@Repository
public interface ServerInfoRepository extends JpaRepository<ServerInfo, Integer> {

    List<ServerInfo> findByIsActivatedTrue();

    List<ServerInfo> findByIsActivatedTrueAndLastHeartbeatAtAfter(LocalDateTime cutoff);

    Optional<ServerInfo> findByServerIpAndServerPort(String serverIp, String serverPort);

    boolean existsByServerIpAndServerPort(String serverIp, String serverPort);
}
