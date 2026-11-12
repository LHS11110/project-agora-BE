package com.endpoint.frelog.domain.irade;

/** ISO 7064 MOD 11-2 validation supplements the server-verified OAuth response; IDs are never accepted as login proof. */
public record OrcidIdentity(String id,String name,String issuer) {
    public OrcidIdentity {
        if(id==null||!id.matches("\\d{4}-\\d{4}-\\d{4}-\\d{3}[\\dX]"))throw new IllegalArgumentException("Invalid ORCID identifier");
        String digits=id.replace("-","");int total=0;
        for(int i=0;i<15;i++)total=(total+digits.charAt(i)-'0')*2;
        int check=(12-total%11)%11;
        if(digits.charAt(15)!=(check==10?'X':(char)('0'+check)))throw new IllegalArgumentException("Invalid ORCID checksum");
        if(!"https://orcid.org".equals(issuer)&&!"https://sandbox.orcid.org".equals(issuer))throw new IllegalArgumentException("Invalid ORCID issuer");
        name=name==null||name.isBlank()?"ORCID Researcher":name.trim();
        name=name.substring(0,Math.min(name.length(),100));
    }
}
