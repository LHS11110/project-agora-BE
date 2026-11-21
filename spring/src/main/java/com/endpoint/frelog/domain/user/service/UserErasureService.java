package com.endpoint.frelog.domain.user.service;

import com.endpoint.frelog.domain.user.entity.*;
import jakarta.persistence.EntityManager;
import org.springframework.stereotype.Service;
import org.springframework.transaction.annotation.Transactional;

/** Retains only a tombstone for historical foreign-key references. */
@Service
public class UserErasureService {
    private final EntityManager entityManager;
    private final UserProfileImageService images;
    public UserErasureService(EntityManager entityManager,UserProfileImageService images){this.entityManager=entityManager;this.images=images;}
    @Transactional public void erase(User user){
        if(user.getStatus()!=UserStatus.WITHDRAWN)throw new IllegalArgumentException("Only withdrawn users can be erased");
        Long id=user.getUserId();String email=user.getEmail();
        // Follow the email-proof -> user ordering used by signup.
        if(email!=null)entityManager.createQuery("delete from EmailProof p where p.email=:email").setParameter("email",email).executeUpdate();
        for(String type:new String[]{"QrLogin","MfaChallenge","MfaState","PasskeyChallenge","PasskeyCredential","UserSession","AuthSession"})
            entityManager.createQuery("delete from "+type+" p where p.userId=:id").setParameter("id",id).executeUpdate();
        entityManager.createNativeQuery("SELECT user_id FROM dbo.users WITH(UPDLOCK,HOLDLOCK) WHERE user_id=:id").setParameter("id",id).getResultList();
        entityManager.createNativeQuery("DELETE FROM dbo.user_canvas_activity WHERE user_id=:id").setParameter("id",id).executeUpdate();
        images.remove(id);
        user.erasePersonalData();
    }
}
