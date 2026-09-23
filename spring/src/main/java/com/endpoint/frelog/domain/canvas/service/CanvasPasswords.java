package com.endpoint.frelog.domain.canvas.service;

import org.springframework.security.crypto.bcrypt.BCryptPasswordEncoder;

import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.util.HexFormat;

/** Password formats shared with the C++ realtime server. */
public final class CanvasPasswords {
    private static final BCryptPasswordEncoder BCRYPT = new BCryptPasswordEncoder();

    private CanvasPasswords() {}

    public static String hashForStorage(String value) {
        if (value == null || value.isBlank()) return null;
        return BCRYPT.encode(value);
    }

    public static String normalizeStoredHash(String value) {
        if (value == null || value.isBlank()) return null;
        return isHash(value) ? value : hashForStorage(value);
    }

    public static boolean isHash(String value) {
        return value != null && (value.matches("\\$2[aby]\\$\\d{2}\\$[./A-Za-z0-9]{53}")
                || value.matches("pbkdf2\\$\\d+\\$[0-9a-f]{32}\\$[0-9a-f]{64}"));
    }

    public static boolean matches(String supplied, String stored) {
        if (supplied == null || stored == null) return false;
        if (stored.startsWith("$2")) return BCRYPT.matches(supplied, stored);
        if (stored.startsWith("pbkdf2$")) {
            try {
                String[] parts = stored.split("\\$");
                if (parts.length != 4 || !isHash(stored)) return false;
                int iterations = Integer.parseInt(parts[1]);
                if (iterations < 100_000 || iterations > 1_000_000) return false;
                byte[] salt = HexFormat.of().parseHex(parts[2]);
                byte[] expected = HexFormat.of().parseHex(parts[3]);
                PBEKeySpec spec = new PBEKeySpec(supplied.toCharArray(), salt, iterations, expected.length * 8);
                try {
                    byte[] actual = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
                    return MessageDigest.isEqual(actual, expected);
                } finally {
                    spec.clearPassword();
                }
            } catch (Exception ignored) {
                return false;
            }
        }
        // Read compatibility for documents created before canvas passwords were hashed.
        return MessageDigest.isEqual(supplied.getBytes(StandardCharsets.UTF_8), stored.getBytes(StandardCharsets.UTF_8));
    }
}
