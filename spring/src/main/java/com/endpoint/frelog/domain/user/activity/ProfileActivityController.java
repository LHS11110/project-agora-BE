package com.endpoint.frelog.domain.user.activity;
import com.endpoint.frelog.global.security.CustomUserDetails;
import com.endpoint.frelog.global.exception.*;
import org.springframework.web.bind.annotation.*;
import org.springframework.security.core.annotation.AuthenticationPrincipal;
import java.util.Map;
@RestController
@RequestMapping("/api/users/me/activity")
public class ProfileActivityController {
 private final ProfileActivityService activity;
 public ProfileActivityController(ProfileActivityService activity){this.activity=activity;}
 @GetMapping public Map<String,Object> get(@AuthenticationPrincipal CustomUserDetails user){
  if(user==null||user.getUserId()==null||user.getUser()==null||!user.getUser().isActive())throw new CustomException(ErrorCode.UNAUTHORIZED);
  return activity.read(user.getUserId());
 }
}
