package com.endpoint.frelog.global.config;

import com.endpoint.frelog.domain.user.entity.Role;
import com.endpoint.frelog.domain.user.entity.User;
import com.endpoint.frelog.domain.user.entity.UserStatus;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import org.slf4j.Logger;
import org.slf4j.LoggerFactory;
import org.springframework.boot.CommandLineRunner;
import org.springframework.security.crypto.password.PasswordEncoder;
import org.springframework.stereotype.Component;

@Component
public class DataInitializer implements CommandLineRunner {

    private static final Logger log = LoggerFactory.getLogger(DataInitializer.class);

    private final UserRepository userRepository;
    private final PasswordEncoder passwordEncoder;

    public DataInitializer(UserRepository userRepository, PasswordEncoder passwordEncoder) {
        this.userRepository = userRepository;
        this.passwordEncoder = passwordEncoder;
    }

    @Override
    public void run(String... args) {
        if (userRepository.count() == 0) {
            log.info("테스트용 초기 계정 데이터를 생성합니다.");

            // 1. 일반 사용자 (ACTIVE)
            User user = new User("user@agora.com", passwordEncoder.encode("password123"), "아고라유저", Role.ROLE_USER);
            user.setStatus(UserStatus.ACTIVE);
            userRepository.save(user);

            // 2. 관리자 (ACTIVE)
            User admin = new User("admin@agora.com", passwordEncoder.encode("admin123"), "아고라관리자", Role.ROLE_ADMIN);
            admin.setStatus(UserStatus.ACTIVE);
            userRepository.save(admin);

            // 3. 정지 계정 (SUSPENDED)
            User suspended = new User("suspended@agora.com", passwordEncoder.encode("password123"), "정지회원", Role.ROLE_USER);
            suspended.setStatus(UserStatus.SUSPENDED);
            userRepository.save(suspended);

            log.info("초기 테스트 계정 3건 생성 완료: user@agora.com, admin@agora.com, suspended@agora.com");
        }
    }
}
