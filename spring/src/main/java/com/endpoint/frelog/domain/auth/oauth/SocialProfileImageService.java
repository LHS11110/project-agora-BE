package com.endpoint.frelog.domain.auth.oauth;

import com.endpoint.frelog.domain.user.repository.UserRepository;
import com.endpoint.frelog.domain.user.service.UserProfileImageService;
import org.springframework.stereotype.Service;
import org.springframework.transaction.event.TransactionalEventListener;
import org.springframework.transaction.PlatformTransactionManager;
import org.springframework.transaction.support.TransactionTemplate;
import org.springframework.transaction.TransactionDefinition;
import java.net.*;
import java.util.Set;

/** Optional social avatar import; never persist a remote URL or fail account creation for an image. */
@Service
public class SocialProfileImageService {
    public record ImportImage(Long userId,String url){}
    private final UserRepository users;
    private final UserProfileImageService images;
    private final TransactionTemplate transaction;
    public SocialProfileImageService(UserRepository users,UserProfileImageService images,PlatformTransactionManager manager){
        this.users=users;this.images=images;transaction=new TransactionTemplate(manager);transaction.setPropagationBehavior(TransactionDefinition.PROPAGATION_REQUIRES_NEW);
    }
    static URI approvedUrl(String value){
        URI input=URI.create(value);
        if(!Set.of("https","http").contains(input.getScheme())||input.getHost()==null
                ||!Set.of("k.kakaocdn.net","img1.kakaocdn.net","img2.kakaocdn.net","t1.kakaocdn.net","phinf.pstatic.net","ssl.pstatic.net","profile-phinf.pstatic.net","phinf.naver.net","lh3.googleusercontent.com","lh4.googleusercontent.com","lh5.googleusercontent.com","lh6.googleusercontent.com").contains(input.getHost())
                ||input.getUserInfo()!=null||input.getPort()!=-1||input.getFragment()!=null)throw new IllegalArgumentException("Unsupported profile image URL");
        return URI.create("https"+value.substring(value.indexOf(':')));
    }
    byte[] download(URI uri) throws Exception {
        for(InetAddress address:InetAddress.getAllByName(uri.getHost())){
            byte[] bytes=address.getAddress();
            if(address.isAnyLocalAddress()||address.isLoopbackAddress()||address.isLinkLocalAddress()||address.isSiteLocalAddress()||address.isMulticastAddress()||(bytes.length==16&&(bytes[0]&0xfe)==0xfc))throw new IllegalArgumentException("Non-public image address");
        }
        var connection=(HttpURLConnection)uri.toURL().openConnection();
        connection.setConnectTimeout(2000);connection.setReadTimeout(3000);connection.setInstanceFollowRedirects(false);
        try {
            if(connection.getResponseCode()!=200||connection.getContentLengthLong()>5L*1024*1024)throw new IllegalArgumentException("Image unavailable");
            try(var input=connection.getInputStream()){
                byte[] bytes=input.readNBytes(5*1024*1024+1);
                if(bytes.length==0||bytes.length>5*1024*1024)throw new IllegalArgumentException("Image size limit");
                return bytes;
            }
        }finally{connection.disconnect();}
    }
    @TransactionalEventListener
    public void importImage(ImportImage event){
        try {
            byte[] bytes=download(approvedUrl(event.url()));
            transaction.executeWithoutResult(status->{
                var user=users.lockById(event.userId()).orElse(null);
                if(user!=null&&user.isActive()&&images.read(event.userId())==null)images.saveBytes(event.userId(),bytes);
            });
        }catch(Exception ignored){
            // Avatar is optional. Keep the local default; do not log personal URLs.
        }
    }
}
