package com.endpoint.frelog.domain.auth.passkey;

import com.yubico.webauthn.*;
import com.yubico.webauthn.data.*;
import com.endpoint.frelog.domain.user.repository.UserRepository;
import org.springframework.stereotype.Component;
import java.util.*;
import java.util.stream.Collectors;

@Component
public class PasskeyStore implements CredentialRepository {
    private final PasskeyCredentialRepository keys;
    private final UserRepository users;
    public PasskeyStore(PasskeyCredentialRepository keys,UserRepository users){this.keys=keys;this.users=users;}
    static ByteArray bytes(String base64) {
        try {return ByteArray.fromBase64Url(base64);} catch(Exception e){throw new IllegalArgumentException("Invalid passkey encoding");}
    }
    private RegisteredCredential registered(PasskeyCredential p) {
        return RegisteredCredential.builder().credentialId(bytes(p.credentialId)).userHandle(bytes(p.userHandle))
                .publicKeyCose(bytes(p.publicKey)).signatureCount(p.signatureCount).build();
    }
    @Override public Set<PublicKeyCredentialDescriptor> getCredentialIdsForUsername(String username){
        return keys.findByUserIdOrderByCreatedAtAsc(Long.valueOf(username)).stream().map(p->PublicKeyCredentialDescriptor.builder().id(bytes(p.credentialId)).build()).collect(Collectors.toSet());
    }
    @Override public Optional<ByteArray> getUserHandleForUsername(String username){
        return keys.findByUserIdOrderByCreatedAtAsc(Long.valueOf(username)).stream().findFirst().map(p->bytes(p.userHandle));
    }
    @Override public Optional<String> getUsernameForUserHandle(ByteArray handle){
        return keys.findFirstByUserHandle(handle.getBase64Url()).filter(p->users.findById(p.userId).map(u->u.isActive()).orElse(false)).map(p->p.userId.toString());
    }
    @Override public Optional<RegisteredCredential> lookup(ByteArray credentialId,ByteArray userHandle){
        return keys.findByCredentialHash(PasskeyService.hash(credentialId.getBase64Url())).filter(p->p.credentialId.equals(credentialId.getBase64Url())&&p.userHandle.equals(userHandle.getBase64Url())).map(this::registered);
    }
    @Override public Set<RegisteredCredential> lookupAll(ByteArray credentialId){
        return keys.findByCredentialHash(PasskeyService.hash(credentialId.getBase64Url())).map(this::registered).map(Set::of).orElseGet(Set::of);
    }
}
