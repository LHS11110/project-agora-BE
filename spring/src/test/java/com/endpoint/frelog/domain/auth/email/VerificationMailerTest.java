package com.endpoint.frelog.domain.auth.email;
import org.junit.jupiter.api.Test;
import org.springframework.test.util.ReflectionTestUtils;
import org.springframework.mail.javamail.JavaMailSenderImpl;
import static org.assertj.core.api.Assertions.*;
class VerificationMailerTest {
 @Test void requiresAuthenticatedEncryptedSmtpAndValidSender(){var m=new VerificationMailer("smtp.example.invalid",587,"user","fixture-password","noreply@example.invalid","starttls");assertThat(m.available()).isTrue();var props=((JavaMailSenderImpl)ReflectionTestUtils.getField(m,"sender")).getJavaMailProperties();assertThat(props.getProperty("mail.smtp.starttls.required")).isEqualTo("true");assertThat(props.getProperty("mail.smtp.ssl.checkserveridentity")).isEqualTo("true");assertThat(props.getProperty("mail.smtp.auth")).isEqualTo("true");assertThat(new VerificationMailer("",587,"","","","starttls").available()).isFalse();assertThatThrownBy(()->new VerificationMailer("host",25,"user","pass","sender@example.invalid","none")).isInstanceOf(IllegalArgumentException.class);}
}
