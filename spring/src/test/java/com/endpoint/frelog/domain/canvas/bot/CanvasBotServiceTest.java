package com.endpoint.frelog.domain.canvas.bot;
import com.endpoint.frelog.domain.canvas.dto.CanvasDocument;
import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.CustomException;
import org.junit.jupiter.api.Test;
import java.util.*;
import static org.junit.jupiter.api.Assertions.*;
import static org.mockito.Mockito.*;
class CanvasBotServiceTest {
 final UserRepository users=mock(UserRepository.class);final CanvasService canvases=mock(CanvasService.class);final CanvasBotService service=new CanvasBotService(users,canvases);
 User owner=new User("owner@example.invalid","hash","Owner",1,Role.ROLE_USER);
 CanvasBotService.Created create(){
  owner.setUserId(1L);var doc=new CanvasDocument();doc.setAdminUserId(1L);doc.setPeople(List.of(1L,9L));when(canvases.getCanvasDocument(eq(12),any())).thenReturn(doc);
  when(users.findByOauthProviderAndOauthIdStartingWith(anyString(),anyString())).thenReturn(List.of());when(users.findMaxTagNumberByNicknameForUpdate(anyString())).thenReturn(0);
  when(users.saveAndFlush(any())).thenAnswer(inv->{User bot=inv.getArgument(0);bot.setUserId(9L);when(users.findById(9L)).thenReturn(Optional.of(bot));return bot;});when(users.findById(1L)).thenReturn(Optional.of(owner));
  return service.create(12,"helper",new CustomUserDetails(owner));
 }
 @Test void credentialsAreHashedCanvasScopedAndRevocable(){
  var created=create();assertTrue(created.token().matches("agora_bot_9\\.[A-Za-z0-9_-]{43}"));User bot=users.findById(9L).orElseThrow();assertFalse(bot.getPasswordHash().contains(created.token().split("\\.")[1]));
  var identity=service.authenticate(created.token());assertNotNull(identity);assertEquals(12,identity.canvasId());assertTrue(service.allowed(identity,"POST","/api/canvases/12/access"));
  for(String path:List.of("/api/canvases/13/access","/api/canvases/12/bots","/api/auth/login","/api/users"))assertFalse(service.allowed(identity,"POST",path));
  service.revoke(12,9L,new CustomUserDetails(owner));assertNull(service.authenticate(created.token()));assertNull(bot.getPasswordHash());
 }
 @Test void tokenRotationInvalidatesOldSecretAndCreatorSuspensionDisablesNewSecret(){
  var old=create();var next=service.rotate(12,9,new CustomUserDetails(owner));assertNull(service.authenticate(old.token()));assertNotNull(service.authenticate(next.token()));owner.setStatus(UserStatus.SUSPENDED);assertNull(service.authenticate(next.token()));
 }
 @Test void nonManagersAndBotsCannotMintCredentials(){
  owner.setUserId(2L);var doc=new CanvasDocument();doc.setAdminUserId(1L);when(canvases.getCanvasDocument(eq(12),any())).thenReturn(doc);
  assertThrows(CustomException.class,()->service.create(12,"helper",new CustomUserDetails(owner)));verifyNoInteractions(users);
  owner.setOauthProvider(CanvasBotService.PROVIDER);assertThrows(CustomException.class,()->service.create(12,"helper",new CustomUserDetails(owner)));
 }
 @Test void invalidCredentialsAreNeverAccepted(){for(String token:List.of("", "agora_bot_9.bad", "agora_bot_9."+"a".repeat(43)))assertNull(service.authenticate(token));}
}
