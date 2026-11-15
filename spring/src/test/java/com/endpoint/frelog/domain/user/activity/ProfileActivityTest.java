package com.endpoint.frelog.domain.user.activity;
import org.junit.jupiter.api.Test;
import org.springframework.jdbc.core.JdbcTemplate;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.domain.user.entity.User;
import static org.assertj.core.api.Assertions.*;
import static org.mockito.Mockito.*;
class ProfileActivityTest {
 @Test void onlyAuthenticatedAccountCanReadItsOwnActivity(){var service=mock(ProfileActivityService.class);var controller=new ProfileActivityController(service);assertThatThrownBy(()->controller.get(null)).isInstanceOf(RuntimeException.class);verifyNoInteractions(service);var principal=mock(CustomUserDetails.class);var user=mock(User.class);when(principal.getUserId()).thenReturn(7L);when(principal.getUser()).thenReturn(user);when(user.isActive()).thenReturn(true);controller.get(principal);verify(service).read(7L);}
 @Test void activityApiIsReadOnlyAndReportsCppCollectionAndThirtyMinutePersistence(){var result=new ProfileActivityService(mock(JdbcTemplate.class)).read(7);assertThat(result.get("days")).isEqualTo(java.util.Map.of());assertThat(result.get("collector")).isEqualTo("cpp");assertThat(result.get("flushIntervalSeconds")).isEqualTo(1800);assertThat(result).doesNotContainKey("github");assertThat(ProfileActivityController.class.getDeclaredMethods()).noneMatch(m->m.isAnnotationPresent(org.springframework.web.bind.annotation.PostMapping.class)||m.isAnnotationPresent(org.springframework.web.bind.annotation.PutMapping.class));}
}
