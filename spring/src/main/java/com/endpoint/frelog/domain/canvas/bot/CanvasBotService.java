package com.endpoint.frelog.domain.canvas.bot;

import com.endpoint.frelog.domain.canvas.service.CanvasService;
import com.endpoint.frelog.domain.canvas.service.CanvasGroupPermissions;
import com.endpoint.frelog.domain.user.entity.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.global.exception.*;
import com.endpoint.frelog.global.security.CustomUserDetails;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;
import java.nio.charset.StandardCharsets;
import java.security.*;
import java.util.*;

/** Canvas-scoped service accounts. Existing users supply the broker's authoritative identity. */
@Service
public class CanvasBotService {
    public static final String PROVIDER="agora-bot", PREFIX="agora_bot_";
    private final UserRepository users;
    private final CanvasService canvases;
    private final SecureRandom random=new SecureRandom();
    public CanvasBotService(UserRepository users,CanvasService canvases){this.users=users;this.canvases=canvases;}
    public record Bot(Long user_id,String nickname,Integer tag_number,Integer canvas_id,boolean active) {}
    public record Created(Bot bot,String token) {}
    public record Identity(CustomUserDetails user,int canvasId) {}
    public static boolean isBot(User user){return user!=null&&PROVIDER.equals(user.getOauthProvider());}
    private static String hash(String secret){try{return HexFormat.of().formatHex(MessageDigest.getInstance("SHA-256").digest(secret.getBytes(StandardCharsets.UTF_8)));}catch(NoSuchAlgorithmException e){throw new IllegalStateException(e);}}
    private String secret(){byte[] bytes=new byte[32];random.nextBytes(bytes);return Base64.getUrlEncoder().withoutPadding().encodeToString(bytes);}
    private int canvasId(User user){try{return Integer.parseInt(user.getOauthId().split(":")[0]);}catch(RuntimeException e){throw new CustomException(ErrorCode.UNAUTHORIZED);}}
    private Bot view(User user){return new Bot(user.getUserId(),user.getNickname(),user.getTagNumber(),canvasId(user),user.isActive());}
    private void manager(int canvasId,CustomUserDetails caller){
        if(caller==null||isBot(caller.getUser()))throw new CustomException(ErrorCode.ACCESS_DENIED);
        var doc=canvases.getCanvasDocument(canvasId,caller);
        if(!CanvasGroupPermissions.admin(doc,caller.getUserId())&&caller.getUser().getRole()!=Role.ROLE_ADMIN)throw new CustomException(ErrorCode.ACCESS_DENIED);
    }
    @Transactional public Created create(int canvasId,String name,CustomUserDetails caller){
        manager(canvasId,caller);
        if(name==null||name.isBlank()||name.trim().length()>80||name.trim().getBytes(StandardCharsets.UTF_8).length>194||name.codePoints().anyMatch(Character::isISOControl))throw new CustomException(ErrorCode.INVALID_INPUT_VALUE,"봇 이름은 제어 문자 없이 1~80자, UTF-8 194바이트 이하여야 합니다.");
        if(users.findByOauthProviderAndOauthIdStartingWith(PROVIDER,canvasId+":").size()>=50)throw new CustomException(ErrorCode.BAD_REQUEST,"캔버스당 봇은 50개까지 생성할 수 있습니다.");
        String raw=secret(),nickname="[BOT] "+name.trim();
        int tag=users.findMaxTagNumberByNicknameForUpdate(nickname)+1;
        User bot=new User("bot-"+UUID.randomUUID()+"@bots.invalid","bot-sha256:"+hash(raw),nickname,tag,Role.ROLE_USER);
        bot.setOauthProvider(PROVIDER);bot.setOauthId(canvasId+":"+caller.getUserId()+":"+UUID.randomUUID());
        users.saveAndFlush(bot);
        return new Created(view(bot),PREFIX+bot.getUserId()+"."+raw);
    }
    @Transactional(readOnly=true) public List<Bot> list(int canvasId,CustomUserDetails caller){manager(canvasId,caller);return users.findByOauthProviderAndOauthIdStartingWith(PROVIDER,canvasId+":").stream().map(this::view).toList();}
    private User target(int canvasId,long botId){User user=users.findById(botId).orElseThrow(()->new CustomException(ErrorCode.USER_NOT_FOUND));if(!isBot(user)||canvasId(user)!=canvasId)throw new CustomException(ErrorCode.ACCESS_DENIED);return user;}
    @Transactional public void revoke(int canvasId,long botId,CustomUserDetails caller){manager(canvasId,caller);User user=target(canvasId,botId);user.setStatus(UserStatus.SUSPENDED);user.setPasswordHash(null);users.save(user);}
    @Transactional public Created rotate(int canvasId,long botId,CustomUserDetails caller){manager(canvasId,caller);User user=target(canvasId,botId);if(!user.isActive())throw new CustomException(ErrorCode.BAD_REQUEST,"폐기된 봇은 다시 활성화할 수 없습니다.");String raw=secret();user.setPasswordHash("bot-sha256:"+hash(raw));users.save(user);return new Created(view(user),PREFIX+botId+"."+raw);}
    @Transactional(readOnly=true) public Identity authenticate(String token){
        try{
            if(token==null||!token.matches("agora_bot_[0-9]{1,10}\\.[A-Za-z0-9_-]{43}"))return null;
            String[] parts=token.substring(PREFIX.length()).split("\\.");User user=users.findById(Long.parseLong(parts[0])).orElse(null);
            if(!isBot(user)||!user.isActive()||user.getPasswordHash()==null||!MessageDigest.isEqual(user.getPasswordHash().getBytes(StandardCharsets.UTF_8),("bot-sha256:"+hash(parts[1])).getBytes(StandardCharsets.UTF_8)))return null;
            // Deleting/suspending a creator also disables its delegated account.
            long creator=Long.parseLong(user.getOauthId().split(":")[1]);
            if(!users.findById(creator).map(User::isActive).orElse(false))return null;
            return new Identity(new CustomUserDetails(user),canvasId(user));
        }catch(RuntimeException e){return null;}
    }
    public boolean allowed(Identity bot,String method,String path){
        if(method.equals("GET")&&path.equals("/api/auth/me"))return true;
        String root="/api/canvases/"+bot.canvasId();
        boolean route=(method.equals("GET")&&(path.equals(root)||path.equals(root+"/settings")||path.equals(root+"/preview")||path.equals(root+"/image")||path.equals(root+"/runtime/settings")||path.startsWith(root+"/resources/")))
            ||(method.equals("POST")&&(path.equals(root+"/access")||path.equals(root+"/runtime/execute")||path.equals(root+"/runtime/build")||path.equals(root+"/runtime/reset")));
        if(!route)return false;
        try{var doc=canvases.getCanvasDocument(bot.canvasId(),bot.user());return doc.getPeople()!=null&&doc.getPeople().contains(bot.user().getUserId());}catch(CustomException e){return false;}
    }
}
