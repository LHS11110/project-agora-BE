package com.endpoint.frelog.domain.auth.oauth;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.service.UserProfileImageService;
import org.junit.jupiter.api.Test;
import org.springframework.transaction.*;
import java.net.URI;
import java.util.Optional;
import static org.mockito.Mockito.*;
import static org.assertj.core.api.Assertions.*;
class SocialProfileImageTest {
 UserRepository users=mock(UserRepository.class);UserProfileImageService images=mock(UserProfileImageService.class);PlatformTransactionManager manager=mock(PlatformTransactionManager.class);
 User user(){var u=new User("photo@example.invalid",null,"Photo",1,Role.ROLE_USER);u.setUserId(1L);return u;}
 SocialProfileImageService importer(boolean fail){when(manager.getTransaction(any())).thenReturn(mock(TransactionStatus.class));return new SocialProfileImageService(users,images,manager){@Override byte[] download(URI uri) throws Exception{if(fail)throw new java.io.IOException("fixture");return new byte[]{1,2,3};}};}
 @Test void approvedUrlsUseHttpsAndRejectOtherOrigins(){assertThat(SocialProfileImageService.approvedUrl("http://k.kakaocdn.net/dn/photo.jpg").toString()).isEqualTo("https://k.kakaocdn.net/dn/photo.jpg");for(String url:new String[]{"https://phinf.pstatic.net.attacker.example/photo","https://lh3.googleusercontent.com.attacker.example/photo","https://127.0.0.1/photo","https://k.kakaocdn.net.attacker.example/photo","https://user@k.kakaocdn.net/photo","https://k.kakaocdn.net:8443/photo","file:///tmp/photo"})assertThatThrownBy(()->SocialProfileImageService.approvedUrl(url)).isInstanceOf(IllegalArgumentException.class);}
 @Test void importedPhotoUsesExistingNormalizationStore(){when(users.lockById(1L)).thenReturn(Optional.of(user()));importer(false).importImage(new SocialProfileImageService.ImportImage(1L,"https://k.kakaocdn.net/photo"));verify(images).saveBytes(eq(1L),aryEq());}
 private byte[] aryEq(){return org.mockito.AdditionalMatchers.aryEq(new byte[]{1,2,3});}
 @Test void userEditedPhotoIsNotOverwritten(){when(users.lockById(1L)).thenReturn(Optional.of(user()));when(images.read(1L)).thenReturn(new byte[]{9});importer(false).importImage(new SocialProfileImageService.ImportImage(1L,"https://k.kakaocdn.net/photo"));verify(images,never()).saveBytes(any(),any());}
 @Test void withdrawnAccountCannotAcquirePhotoAfterDeletion(){var u=user();u.setStatus(UserStatus.WITHDRAWN);when(users.lockById(1L)).thenReturn(Optional.of(u));importer(false).importImage(new SocialProfileImageService.ImportImage(1L,"https://k.kakaocdn.net/photo"));verifyNoInteractions(images);}
 @Test void optionalPhotoFailureDoesNotFailSignup(){assertThatCode(()->importer(true).importImage(new SocialProfileImageService.ImportImage(1L,"https://k.kakaocdn.net/photo"))).doesNotThrowAnyException();verifyNoInteractions(users,images);}
}
