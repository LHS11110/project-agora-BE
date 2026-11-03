package com.endpoint.frelog.domain.auth.oauth;

import org.springframework.security.oauth2.core.user.OAuth2User;
import org.springframework.security.oauth2.core.oidc.user.OidcUser;
import org.springframework.security.oauth2.core.*;
import java.util.*;

/** Constructed exclusively from the server-side OAuth user-info / verified OIDC principal. */
public record SocialIdentity(String provider,String subject,String email,String name,boolean emailVerified,String profileImageUrl) {
    public SocialIdentity(String provider,String subject,String email,String name,boolean emailVerified){this(provider,subject,email,name,emailVerified,null);}
    public static final Set<String> PROVIDERS=Set.of("google","naver","kakao");
    private static String string(Object value){return value instanceof String s?s:null;}
    private static Map<?,?> map(Object value){return value instanceof Map<?,?> m?m:Map.of();}
    public static SocialIdentity from(String provider,OAuth2User principal){
        SocialIdentity identity;
        if("google".equals(provider)&&principal instanceof OidcUser oidc){
            if(!Boolean.TRUE.equals(oidc.getEmailVerified()))throw invalid();
            identity=new SocialIdentity(provider,oidc.getSubject(),oidc.getEmail(),oidc.getFullName(),true,oidc.getPicture());
        }else if("naver".equals(provider)){
            var attributes=principal.getAttributes();if(!"00".equals(attributes.get("resultcode")))throw invalid();
            var profile=map(attributes.get("response"));String name=string(profile.get("nickname"));if(name==null||name.isBlank())name=string(profile.get("name"));
            // Naver does not expose a verified-email flag. First signup uses our email proof.
            identity=new SocialIdentity(provider,string(profile.get("id")),string(profile.get("email")),name,false,string(profile.get("profile_image")));
        }else if("kakao".equals(provider)){
            var attributes=principal.getAttributes();Object id=attributes.get("id");
            if(!(id instanceof Number)||!id.toString().matches("[1-9][0-9]*"))throw invalid();
            var account=map(attributes.get("kakao_account"));var profile=map(account.get("profile"));
            String nickname=Boolean.TRUE.equals(account.get("profile_nickname_needs_agreement"))||Boolean.TRUE.equals(profile.get("is_default_nickname"))?null:string(profile.get("nickname"));
            String image=null;
            if(!Boolean.TRUE.equals(account.get("profile_image_needs_agreement"))&&!Boolean.TRUE.equals(profile.get("is_default_image"))){
                image=string(profile.get("profile_image_url"));
                if(image==null||image.isBlank())image=string(profile.get("thumbnail_image_url"));
            }
            identity=new SocialIdentity(provider,id.toString(),string(account.get("email")),nickname,Boolean.TRUE.equals(account.get("is_email_valid"))&&Boolean.TRUE.equals(account.get("is_email_verified")),image);
        }else throw invalid();
        identity.validate();return identity;
    }
    public void validate(){if(provider==null||!PROVIDERS.contains(provider)||subject==null||subject.isBlank()||subject.length()>255||subject.chars().anyMatch(Character::isISOControl))throw invalid();}
    public boolean hasVerifiedEmail(){return emailVerified&&email!=null&&email.length()<=255&&email.matches("[^\\s@]+@[^\\s@]+\\.[^\\s@]+");}
    public SocialIdentity withVerifiedEmail(String address){return new SocialIdentity(provider,subject,address,name,true,profileImageUrl);}
    private static OAuth2AuthenticationException invalid(){return new OAuth2AuthenticationException(new OAuth2Error("invalid_user_info"));}
}
