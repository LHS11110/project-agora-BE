package com.endpoint.frelog.domain.loadbalancer.service;

import com.endpoint.frelog.domain.canvas.client.CppServerClient;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateRedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.AllocateServerResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.DatabaseAddressResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.RedisResponse;
import com.endpoint.frelog.domain.loadbalancer.dto.RegisterRedisRequest;
import com.endpoint.frelog.domain.loadbalancer.dto.RegisterServerRequest;
import com.endpoint.frelog.domain.loadbalancer.dto.ServerResponse;
import com.endpoint.frelog.domain.loadbalancer.entity.RedisInfo;
import com.endpoint.frelog.domain.loadbalancer.entity.ServerInfo;
import com.endpoint.frelog.domain.loadbalancer.repository.RedisInfoRepository;
import com.endpoint.frelog.domain.loadbalancer.repository.ServerInfoRepository;
import com.endpoint.frelog.global.config.DatabaseProperties;
import com.endpoint.frelog.global.exception.CustomException;
import com.endpoint.frelog.global.exception.ErrorCode;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

import java.io.BufferedReader;
import java.io.InputStream;
import java.io.InputStreamReader;
import java.io.OutputStream;
import java.net.InetSocketAddress;
import java.net.Socket;
import java.nio.charset.StandardCharsets;
import java.util.List;
import java.util.concurrent.ThreadLocalRandom;

@Service
public class LoadBalancerService {

    private static final Logger log = LoggerFactory.getLogger(LoadBalancerService.class);

    private final ServerInfoRepository serverInfoRepository;
    private final RedisInfoRepository redisInfoRepository;
    private final CanvasInfoRepository canvasInfoRepository;
    private final CppServerClient cppServerClient;
    private final DatabaseProperties databaseProperties;

    public LoadBalancerService(
            ServerInfoRepository serverInfoRepository,
            RedisInfoRepository redisInfoRepository,
            CanvasInfoRepository canvasInfoRepository,
            CppServerClient cppServerClient,
            DatabaseProperties databaseProperties) {
        this.serverInfoRepository = serverInfoRepository;
        this.redisInfoRepository = redisInfoRepository;
        this.canvasInfoRepository = canvasInfoRepository;
        this.cppServerClient = cppServerClient;
        this.databaseProperties = databaseProperties;
    }

    /**
     * Power of Two Choices (P2C) 기반 Server 할당 (IP, Port 반환)
     * is_activated == true 인 행에 대해서만 로드 밸런싱 수행
     */
    @Transactional(readOnly = true)
    public AllocateServerResponse allocateServer() {
        List<ServerInfo> servers = serverInfoRepository.findByIsActivatedTrue();

        if (servers.isEmpty()) {
            throw new CustomException(ErrorCode.NO_SERVER_AVAILABLE, "활성화된 C++ 서버가 없습니다.");
        }

        if (servers.size() == 1) {
            ServerInfo single = servers.get(0);
            log.info("C++ Server 단일 등록 인스턴스 할당: {}:{}", single.getServerIp(), single.getServerPort());
            return AllocateServerResponse.of(single.getServerIp(), single.getServerPort(), single.getWsPort());
        }

        // Power of Two Choices: 무작위로 2개 후보 선택
        int size = servers.size();
        int idx1 = ThreadLocalRandom.current().nextInt(size);
        int idx2 = ThreadLocalRandom.current().nextInt(size - 1);
        if (idx2 >= idx1) {
            idx2++;
        }

        ServerInfo s1 = servers.get(idx1);
        ServerInfo s2 = servers.get(idx2);

        int load1 = cppServerClient.getCanvasCountFromServer(s1.getServerIp(), s1.getServerPort());
        int load2 = cppServerClient.getCanvasCountFromServer(s2.getServerIp(), s2.getServerPort());

        ServerInfo chosen = (load1 <= load2) ? s1 : s2;
        log.info("C++ Server 로드밸런싱 (P2C): [{}:{}] (부하 {}) vs [{}:{}] (부하 {}) -> 선택: [{}:{}]",
                s1.getServerIp(), s1.getServerPort(), load1,
                s2.getServerIp(), s2.getServerPort(), load2,
                chosen.getServerIp(), chosen.getServerPort());

        return AllocateServerResponse.of(chosen.getServerIp(), chosen.getServerPort(), chosen.getWsPort());
    }

    /**
     * Power of Two Choices (P2C) 기반 Redis 할당 (IP, Port 반환)
     * is_activated == true 인 행에 대해서만 로드 밸런싱 수행
     */
    @Transactional(readOnly = true)
    public AllocateRedisResponse allocateRedis() {
        List<RedisInfo> redisList = redisInfoRepository.findByIsActivatedTrue();

        if (redisList.isEmpty()) {
            throw new CustomException(ErrorCode.NO_REDIS_AVAILABLE, "활성화된 Redis 서버가 없습니다.");
        }

        if (redisList.size() == 1) {
            RedisInfo single = redisList.get(0);
            log.info("Redis 단일 등록 인스턴스 할당: {}:{}", single.getRedisIp(), single.getRedisPort());
            return AllocateRedisResponse.of(single.getRedisIp(), single.getRedisPort());
        }

        // Power of Two Choices: 무작위로 2개 후보 선택
        int size = redisList.size();
        int idx1 = ThreadLocalRandom.current().nextInt(size);
        int idx2 = ThreadLocalRandom.current().nextInt(size - 1);
        if (idx2 >= idx1) {
            idx2++;
        }

        RedisInfo r1 = redisList.get(idx1);
        RedisInfo r2 = redisList.get(idx2);

        int load1 = getRedisCanvasCountInJava(r1.getRedisIp(), r1.getRedisPort());
        int load2 = getRedisCanvasCountInJava(r2.getRedisIp(), r2.getRedisPort());

        RedisInfo chosen = (load1 <= load2) ? r1 : r2;
        log.info("Redis 로드밸런싱 (Java P2C): [{}:{}] (부하 {}) vs [{}:{}] (부하 {}) -> 선택: [{}:{}]",
                r1.getRedisIp(), r1.getRedisPort(), load1,
                r2.getRedisIp(), r2.getRedisPort(), load2,
                chosen.getRedisIp(), chosen.getRedisPort());

        return AllocateRedisResponse.of(chosen.getRedisIp(), chosen.getRedisPort());
    }

    /**
     * 자바(Spring)에서 특정 Redis 인스턴스의 캔버스 부하(개수)를 직접 측정
     */
    public int getRedisCanvasCountInJava(String redisIp, String redisPort) {
        int socketKeyCount = probeRedisKeyCount(redisIp, redisPort);
        if (socketKeyCount >= 0) {
            return socketKeyCount;
        }

        long dbCount = canvasInfoRepository.countByRedisInfo_RedisIpAndRedisInfo_RedisPort(redisIp, redisPort);
        return (int) dbCount;
    }

    private int probeRedisKeyCount(String host, String portStr) {
        int port;
        try {
            port = Integer.parseInt(portStr);
        } catch (NumberFormatException e) {
            return -1;
        }

        try (Socket socket = new Socket()) {
            socket.connect(new InetSocketAddress(host, port), 400);
            socket.setSoTimeout(400);
            OutputStream out = socket.getOutputStream();
            InputStream in = socket.getInputStream();

            String authCmd = "*2\r\n$4\r\nAUTH\r\n$28\r\nAgoraRedisSecret@Passw0rd!2026\r\n";
            out.write(authCmd.getBytes(StandardCharsets.UTF_8));
            out.flush();
            byte[] buf = new byte[256];
            in.read(buf);

            String keysCmd = "*2\r\n$4\r\nKEYS\r\n$7\r\ncanvas*\r\n";
            out.write(keysCmd.getBytes(StandardCharsets.UTF_8));
            out.flush();

            BufferedReader reader = new BufferedReader(new InputStreamReader(in, StandardCharsets.UTF_8));
            String line = reader.readLine();
            if (line != null && line.startsWith("*")) {
                int arraySize = Integer.parseInt(line.substring(1));
                return Math.max(0, arraySize);
            }
            return -1;
        } catch (Exception e) {
            log.debug("Redis Java Socket 직접 프로브 미응답({}:{}): {}", host, portStr, e.getMessage());
            return -1;
        }
    }

    public DatabaseAddressResponse getDatabaseAddress() {
        return DatabaseAddressResponse.of(
                databaseProperties.getHost(),
                databaseProperties.getPort(),
                databaseProperties.getName(),
                databaseProperties.getAddress()
        );
    }

    // =========================================================================
    // C++ Server 관리 (CRUD) - 관리자 전용
    // =========================================================================

    @Transactional(readOnly = true)
    public List<ServerResponse> listServers() {
        return serverInfoRepository.findAll().stream()
                .map(s -> {
                    int load = cppServerClient.getCanvasCountFromServer(s.getServerIp(), s.getServerPort());
                    return ServerResponse.from(s, load == Integer.MAX_VALUE ? 0 : load);
                })
                .toList();
    }

    @Transactional(readOnly = true)
    public ServerResponse getServerById(Long serverId) {
        ServerInfo server = serverInfoRepository.findById(serverId)
                .orElseThrow(() -> new CustomException(ErrorCode.SERVER_NOT_FOUND, "C++ 서버를 찾을 수 없습니다: " + serverId));
        int load = cppServerClient.getCanvasCountFromServer(server.getServerIp(), server.getServerPort());
        return ServerResponse.from(server, load == Integer.MAX_VALUE ? 0 : load);
    }

    @Transactional
    public ServerResponse registerServer(RegisterServerRequest request) {
        String ip = request.serverIp().trim();
        String port = request.serverPort().trim();
        String wsPort = request.wsPort().trim();
        String name = request.serverName() != null ? request.serverName().trim() : ("Server-" + port);

        ServerInfo server = serverInfoRepository.findByServerIpAndServerPort(ip, port)
                .orElseGet(() -> new ServerInfo(ip, port, wsPort, name));

        server.setWsPort(wsPort);
        server.setIsActivated(true);
        ServerInfo saved = serverInfoRepository.save(server);

        return ServerResponse.from(saved, 0);
    }

    /**
     * C++ 서버 변경:
     * 서버가 이용 중이라면 is_activated를 false로 바꾸고 나머지는 변경을 허용하지 않음,
     * 이용 중이지 않으면 즉시 수행
     */
    @Transactional
    public ServerResponse updateServer(Long serverId, RegisterServerRequest request) {
        ServerInfo server = serverInfoRepository.findById(serverId)
                .orElseThrow(() -> new CustomException(ErrorCode.SERVER_NOT_FOUND, "C++ 서버를 찾을 수 없습니다: " + serverId));

        boolean inUse = canvasInfoRepository.existsByCppServer_ServerIpAndCppServer_ServerPortAndIsCachedTrue(server.getServerIp(), server.getServerPort());

        if (inUse) {
            log.info("C++ 서버 #{}({}:{})가 사용 중이므로 is_activated만 false로 변경하고 다른 필드는 변경하지 않습니다.",
                    serverId, server.getServerIp(), server.getServerPort());
            server.setIsActivated(false);
            ServerInfo saved = serverInfoRepository.save(server);
            return ServerResponse.from(saved, 0);
        }

        if (request.serverIp() != null && !request.serverIp().isBlank()) {
            server.setServerIp(request.serverIp().trim());
        }
        if (request.serverPort() != null && !request.serverPort().isBlank()) {
            server.setServerPort(request.serverPort().trim());
        }
        server.setIsActivated(true);
        ServerInfo saved = serverInfoRepository.save(server);
        return ServerResponse.from(saved, 0);
    }

    /**
     * C++ 서버 삭제:
     * 서버가 이용 중이라면 is_activated를 false로 바꾸고 나머지는 변경을 허용하지 않음,
     * 이용 중이지 않으면 즉시 수행
     */
    @Transactional
    public void deleteServer(Long serverId) {
        ServerInfo server = serverInfoRepository.findById(serverId)
                .orElseThrow(() -> new CustomException(ErrorCode.SERVER_NOT_FOUND, "C++ 서버를 찾을 수 없습니다: " + serverId));

        boolean inUse = canvasInfoRepository.existsByCppServer_ServerIpAndCppServer_ServerPortAndIsCachedTrue(server.getServerIp(), server.getServerPort());

        if (inUse) {
            log.info("C++ 서버 #{}({}:{})가 사용 중이므로 삭제 대신 is_activated=false로 변경합니다.",
                    serverId, server.getServerIp(), server.getServerPort());
            server.setIsActivated(false);
            serverInfoRepository.save(server);
            return;
        }

        serverInfoRepository.delete(server);
        log.info("C++ 서버 #{}({}:{}) 즉시 삭제 완료", serverId, server.getServerIp(), server.getServerPort());
    }

    // =========================================================================
    // Redis 관리 (CRUD) - 관리자 전용
    // =========================================================================

    @Transactional(readOnly = true)
    public List<RedisResponse> listRedis() {
        return redisInfoRepository.findAll().stream()
                .map(r -> {
                    int load = getRedisCanvasCountInJava(r.getRedisIp(), r.getRedisPort());
                    return RedisResponse.from(r, load);
                })
                .toList();
    }

    @Transactional(readOnly = true)
    public RedisResponse getRedisById(Long redisId) {
        RedisInfo redis = redisInfoRepository.findById(redisId)
                .orElseThrow(() -> new CustomException(ErrorCode.REDIS_NOT_FOUND, "Redis 서버를 찾을 수 없습니다: " + redisId));
        int load = getRedisCanvasCountInJava(redis.getRedisIp(), redis.getRedisPort());
        return RedisResponse.from(redis, load);
    }

    @Transactional
    public RedisResponse registerRedis(RegisterRedisRequest request) {
        String ip = request.redisIp().trim();
        String port = request.redisPort().trim();

        RedisInfo redis = redisInfoRepository.findByRedisIpAndRedisPort(ip, port)
                .orElseGet(() -> new RedisInfo(ip, port));

        redis.setIsActivated(true);
        RedisInfo saved = redisInfoRepository.save(redis);

        return RedisResponse.from(saved, 0);
    }

    /**
     * Redis 서버 변경:
     * redis 서버가 이용 중이라면 is_activated를 false로 바꾸고 나머지는 변경을 허용하지 않음,
     * 이용 중이지 않으면 즉시 수행
     */
    @Transactional
    public RedisResponse updateRedis(Long redisId, RegisterRedisRequest request) {
        RedisInfo redis = redisInfoRepository.findById(redisId)
                .orElseThrow(() -> new CustomException(ErrorCode.REDIS_NOT_FOUND, "Redis 서버를 찾을 수 없습니다: " + redisId));

        boolean inUse = canvasInfoRepository.existsByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(redis.getRedisIp(), redis.getRedisPort());

        if (inUse) {
            log.info("Redis 서버 #{}({}:{})가 사용 중이므로 is_activated만 false로 변경하고 다른 필드는 변경하지 않습니다.",
                    redisId, redis.getRedisIp(), redis.getRedisPort());
            redis.setIsActivated(false);
            RedisInfo saved = redisInfoRepository.save(redis);
            return RedisResponse.from(saved, 0);
        }

        if (request.redisIp() != null && !request.redisIp().isBlank()) {
            redis.setRedisIp(request.redisIp().trim());
        }
        if (request.redisPort() != null && !request.redisPort().isBlank()) {
            redis.setRedisPort(request.redisPort().trim());
        }
        redis.setIsActivated(true);
        RedisInfo saved = redisInfoRepository.save(redis);
        return RedisResponse.from(saved, 0);
    }

    /**
     * Redis 서버 삭제:
     * redis 서버가 이용 중이라면 is_activated를 false로 바꾸고 나머지는 변경을 허용하지 않음,
     * 이용 중이지 않으면 즉시 수행
     */
    @Transactional
    public void deleteRedis(Long redisId) {
        RedisInfo redis = redisInfoRepository.findById(redisId)
                .orElseThrow(() -> new CustomException(ErrorCode.REDIS_NOT_FOUND, "Redis 서버를 찾을 수 없습니다: " + redisId));

        boolean inUse = canvasInfoRepository.existsByRedisInfo_RedisIpAndRedisInfo_RedisPortAndIsCachedTrue(redis.getRedisIp(), redis.getRedisPort());

        if (inUse) {
            log.info("Redis 서버 #{}({}:{})가 사용 중이므로 삭제 대신 is_activated=false로 변경합니다.",
                    redisId, redis.getRedisIp(), redis.getRedisPort());
            redis.setIsActivated(false);
            redisInfoRepository.save(redis);
            return;
        }

        redisInfoRepository.delete(redis);
        log.info("Redis 서버 #{}({}:{}) 즉시 삭제 완료", redisId, redis.getRedisIp(), redis.getRedisPort());
    }
}
