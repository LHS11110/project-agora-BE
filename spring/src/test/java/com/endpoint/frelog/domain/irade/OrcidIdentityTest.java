package com.endpoint.frelog.domain.irade;
import org.junit.jupiter.api.Test;
import static org.assertj.core.api.Assertions.*;
class OrcidIdentityTest {
    @Test void checksumAndIssuerAreValidated(){
        assertThat(new OrcidIdentity("0000-0002-1825-0097",null,"https://orcid.org").name()).isEqualTo("ORCID Researcher");
        assertThatThrownBy(()->new OrcidIdentity("0000-0002-1825-0098","Test","https://orcid.org")).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(()->new OrcidIdentity("0000-0002-1825-0097","Test","https://attacker.invalid")).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(()->new OrcidConfiguration(true,"","secret","https://orcid.org","https://frelog.p-e.kr/api/irade/auth/orcid/callback")).isInstanceOf(IllegalArgumentException.class);
        assertThatThrownBy(()->new OrcidConfiguration(true,"id","secret","https://orcid.org","https://frelog.p-e.kr/evil")).isInstanceOf(IllegalArgumentException.class);
        assertThat(new OrcidConfiguration(true,"id","never-log","https://orcid.org","https://frelog.p-e.kr/api/irade/auth/orcid/callback").toString()).doesNotContain("never-log");
    }
}
