package com.endpoint.frelog.domain.canvas.service;

import org.junit.jupiter.api.Test;

import javax.crypto.SecretKeyFactory;
import javax.crypto.spec.PBEKeySpec;
import java.util.HexFormat;

import static org.assertj.core.api.Assertions.assertThat;

class CanvasPasswordsTest {

    @Test
    void newAndLegacyPasswordsAreStoredAsHashes() {
        String created = CanvasPasswords.hashForStorage("secret");
        assertThat(created).startsWith("$2");
        assertThat(CanvasPasswords.matches("secret", created)).isTrue();
        String migrated = CanvasPasswords.normalizeStoredHash("legacy-secret");
        assertThat(migrated).isNotEqualTo("legacy-secret");
        assertThat(CanvasPasswords.matches("legacy-secret", migrated)).isTrue();
    }

    @Test
    void acceptsCppPbkdf2Format() throws Exception {
        byte[] salt = HexFormat.of().parseHex("000102030405060708090a0b0c0d0e0f");
        PBEKeySpec spec = new PBEKeySpec("secret".toCharArray(), salt, 310000, 256);
        byte[] digest = SecretKeyFactory.getInstance("PBKDF2WithHmacSHA256").generateSecret(spec).getEncoded();
        String stored = "pbkdf2$310000$" + HexFormat.of().formatHex(salt) + "$" + HexFormat.of().formatHex(digest);
        assertThat(CanvasPasswords.isHash(stored)).isTrue();
        assertThat(CanvasPasswords.matches("secret", stored)).isTrue();
        assertThat(CanvasPasswords.matches("wrong", stored)).isFalse();
    }
}
