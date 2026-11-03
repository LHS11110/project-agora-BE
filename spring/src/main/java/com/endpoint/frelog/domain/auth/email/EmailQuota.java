package com.endpoint.frelog.domain.auth.email;
import jakarta.persistence.*;
import java.time.LocalDateTime;
@Entity @Table(name="auth_email_ip_quota")
public class EmailQuota {
 @Id @Column(length=64) String id;
 LocalDateTime windowStart;
 int sends;
 protected EmailQuota(){}
 EmailQuota(String id,LocalDateTime now){this.id=id;windowStart=now;}
}
