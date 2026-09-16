package com.endpoint.frelog.domain.loadbalancer.repository;

import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import org.springframework.data.jpa.repository.JpaRepository;
import org.springframework.stereotype.Repository;

import java.util.List;
import java.util.Optional;

@Repository
public interface ServerInfoRepository extends JpaRepository<ServerInfo, Long> {

    List<ServerInfo> findByIsActivatedTrue();

    Optional<ServerInfo> findByServerIpAndServerPort(String serverIp, String serverPort);

    boolean existsByServerIpAndServerPort(String serverIp, String serverPort);
}
