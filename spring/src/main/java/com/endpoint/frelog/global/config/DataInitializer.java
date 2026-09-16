package com.endpoint.frelog.global.config;

import com.endpoint.frelog.domain.canvas.entity.CanvasInfo;
import com.endpoint.frelog.domain.canvas.repository.CanvasInfoRepository;
import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.entity.UserSession;
import com.endpoint.frelog.domain.user.repository.UserSessionRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.beans.factory.annotation.Value;
import org.springframework.boot.CommandLineRunner;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Component;

@Component
public class DataInitializer implements CommandLineRunner {

    private static final Logger log = LoggerFactory.getLogger(DataInitializer.class);

    private final UserRepository userRepository;
    private final UserSessionRepository userSessionRepository;
    private final CanvasInfoRepository canvasInfoRepository;
    private final PasswordEncoder passwordEncoder;

    @Value("${app.admin.email:admin@agora.com}")
    private String adminEmail;
    
    @Value("${app.admin.password:admin123}")
    private String adminPassword;
    
    @Value("${app.admin.nickname:아고라관리자}")
    private String adminNickname;

    public DataInitializer(UserRepository userRepository,
                           UserSessionRepository userSessionRepository,
                           CanvasInfoRepository canvasInfoRepository,
                           PasswordEncoder passwordEncoder) {
        this.userRepository = userRepository;
        this.userSessionRepository = userSessionRepository;
        this.canvasInfoRepository = canvasInfoRepository;
        this.passwordEncoder = passwordEncoder;
    }

    @Override
    public void run(String... args) {
        if (userRepository.count() == 0) {
            log.info("초기 관리자 계정 데이터를 생성합니다.");

            // 1. 관리자 (ACTIVE)
            User admin = new User(adminEmail, passwordEncoder.encode(adminPassword), adminNickname, Role.ROLE_ADMIN);
            admin.setStatus(UserStatus.ACTIVE);
            User savedAdmin = userRepository.save(admin);
            
            userSessionRepository.save(new UserSession(savedAdmin));

            log.info("초기 관리자 계정 1건 생성 완료: {}", adminEmail);

            // 초기 샘플 캔버스 생성 (초기 is_cached=false)
            if (canvasInfoRepository.count() == 0) {
                CanvasInfo sampleCanvas = new CanvasInfo();
                canvasInfoRepository.save(sampleCanvas);
                log.info("초기 테스트 캔버스 1건 생성 완료: #{} (is_cached=false)", sampleCanvas.getCanvasId());
            }
        }
    }
}
