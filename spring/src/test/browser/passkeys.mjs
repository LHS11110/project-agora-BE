import assert from 'node:assert/strict';
import {createHmac} from 'node:crypto';
const {chromium}=await import(process.env.AGORA_E2E_PLAYWRIGHT_MODULE||'playwright-core');
const browser=await chromium.launch({executablePath:process.env.AGORA_E2E_CHROME||'/usr/bin/google-chrome',args:['--no-sandbox']});
try{
 const context=await browser.newContext();
 await context.route('https://frelog.p-e.kr/**',async route=>{
  const request=route.request(),url=new URL(request.url());
  if(!url.pathname.startsWith('/api/'))return route.fulfill({contentType:'text/html',body:'<!doctype html><title>Passkey server integration fixture</title>'});
  const headers=await request.allHeaders();delete headers.host;delete headers['content-length'];
  const response=await fetch(process.env.AGORA_PASSKEY_BACKEND+url.pathname,{method:request.method(),headers,body:request.postData()||undefined});
  const out=Object.fromEntries(response.headers);delete out['content-encoding'];delete out['transfer-encoding'];delete out['content-length'];
  await route.fulfill({status:response.status,headers:out,body:await response.text()});
 });
 const page=await context.newPage();const cdp=await context.newCDPSession(page);await cdp.send('WebAuthn.enable');
 const {authenticatorId}=await cdp.send('WebAuthn.addVirtualAuthenticator',{options:{protocol:'ctap2',transport:'internal',hasResidentKey:true,hasUserVerification:true,isUserVerified:true,automaticPresenceSimulation:true}});
 await page.goto('https://frelog.p-e.kr/');
 await page.evaluate(({token,other})=>{
  window.token=token;window.otherToken=other;
  window.call=async(path,body,auth,method='POST')=>{
   const response=await fetch('/api/auth/'+path,{method,headers:{...(body?{'Content-Type':'application/json'}:{}),...(auth?{Authorization:'Bearer '+auth}:{})},...(body?{body:JSON.stringify(body)}:{})});
   return {status:response.status,body:await response.json().catch(()=>null)};
  };
  window.make=async(registration,options)=>{
   const publicKey=registration?PublicKeyCredential.parseCreationOptionsFromJSON(options.publicKey):PublicKeyCredential.parseRequestOptionsFromJSON(options.publicKey);
   const credential=registration?await navigator.credentials.create({publicKey}):await navigator.credentials.get({publicKey});
   return credential.toJSON();
  };
 },{token:process.env.AGORA_PASSKEY_TOKEN,other:process.env.AGORA_PASSKEY_OTHER_TOKEN});
 const run=fn=>page.evaluate(fn);
 assert.equal((await run(()=>call('passkeys/registration/options',null,null))).status,401);
 const start=await run(()=>call('passkeys/registration/options',null,token));assert.equal(start.status,200);
 assert.equal(start.body.options.publicKey.authenticatorSelection.userVerification,'required');
 assert.equal(start.body.options.publicKey.authenticatorSelection.residentKey,'required');
 const cookies=await context.cookies();const cookie=cookies.find(c=>c.name==='FRELOG_PASSKEY');assert.ok(cookie.httpOnly&&cookie.secure&&cookie.sameSite==='Strict');
 await page.evaluate(value=>window.registration=value,start.body);
 const registered=await run(async()=>{window.attestation=await make(true,registration.options);return call('passkeys/registration/verify',{challengeToken:registration.challengeToken,credential:attestation,name:'Virtual device'},token);});assert.equal(registered.status,200);
 assert.equal((await run(()=>call('passkeys/registration/verify',{challengeToken:registration.challengeToken,credential:attestation,name:'Replay'},token))).status,400);
 let list=await run(()=>call('passkeys',null,token,'GET'));assert.equal(list.body.length,1);await page.evaluate(id=>window.keyId=id,list.body[0].id);
 assert.equal((await run(()=>call('passkeys/'+keyId,null,otherToken,'DELETE'))).status,400);
 const login=async(mutate,weakenUv=false)=>{
  const start=await run(()=>call('passkeys/login/options'));assert.equal(start.status,200);
  await page.evaluate(({value,weakenUv})=>{window.assertionRequest=value;if(weakenUv)assertionRequest.options.publicKey.userVerification='discouraged';},{value:start.body,weakenUv});
  const assertion=await run(async()=>{window.assertion=await make(false,assertionRequest.options);window.cleanAssertion=structuredClone(assertion);return assertion;});
  if(mutate)await page.evaluate(mutate);
  return run(()=>call('passkeys/login/verify',{challengeToken:assertionRequest.challengeToken,credential:assertion}));
 };
 const success=await login();assert.equal(success.status,200);assert.ok(success.body.accessToken);assert.equal(success.body.user.email,'passkey-test@example.invalid');
 await context.addCookies([{name:'FRELOG_PASSKEY',value:await run(()=>assertionRequest.challengeToken),domain:'frelog.p-e.kr',path:'/api/auth/passkeys',secure:true,httpOnly:true,sameSite:'Strict'}]);
 assert.equal((await run(()=>call('passkeys/login/verify',{challengeToken:assertionRequest.challengeToken,credential:cleanAssertion}))).status,400);
 assert.equal((await login(()=>{assertion.response.signature='AAAA';})).status,400);
 // A failed signed challenge is consumed too, even if its response is corrected later.
 await context.addCookies([{name:'FRELOG_PASSKEY',value:await run(()=>assertionRequest.challengeToken),domain:'frelog.p-e.kr',path:'/api/auth/passkeys',secure:true,httpOnly:true,sameSite:'Strict'}]);
 assert.equal((await run(()=>call('passkeys/login/verify',{challengeToken:assertionRequest.challengeToken,credential:cleanAssertion}))).status,400);
 assert.equal((await login(()=>{const d=JSON.parse(atob(assertion.response.clientDataJSON.replace(/-/g,'+').replace(/_/g,'/')));d.origin='https://attacker.example';assertion.response.clientDataJSON=btoa(JSON.stringify(d)).replace(/\+/g,'-').replace(/\//g,'_').replace(/=+$/,'');})).status,400);
 assert.equal((await login(()=>{assertionRequest.challengeToken='wrong-browser-challenge';})).status,400);
 const originResponse=await fetch(process.env.AGORA_PASSKEY_BACKEND+'/api/auth/passkeys/login/options',{method:'POST',headers:{Origin:'https://attacker.example'}});assert.ok([400,403].includes(originResponse.status));
 await cdp.send('WebAuthn.setUserVerified',{authenticatorId,isUserVerified:false});
 // Even if a client weakens the browser options, the signed non-UV response is rejected by the server.
 assert.equal((await login(null,true)).status,400);
 // Check the server's emitted requirement too.
 const required=await run(()=>call('passkeys/login/options'));assert.equal(required.body.options.publicKey.userVerification,'required');
 await cdp.send('WebAuthn.setUserVerified',{authenticatorId,isUserVerified:true});
 // Verified passkeys satisfy authentication even when TOTP is enabled. Password login still requires TOTP.
 const enrollment=await run(()=>call('mfa/enrollment',null,token));assert.equal(enrollment.status,200);
 const alphabet='ABCDEFGHIJKLMNOPQRSTUVWXYZ234567';let bits='';for(const c of enrollment.body.secret)bits+=alphabet.indexOf(c).toString(2).padStart(5,'0');
 const secret=Buffer.from(bits.match(/.{8}/g).map(s=>parseInt(s,2)));const counter=Buffer.alloc(8);counter.writeBigUInt64BE(BigInt(Math.floor(Date.now()/30000)));const mac=createHmac('sha1',secret).update(counter).digest();const offset=mac[19]&15;const otp=((mac.readUInt32BE(offset)&0x7fffffff)%1000000).toString().padStart(6,'0');
 const confirm=await page.evaluate(code=>call('mfa/enrollment/confirm',{code},token),otp);assert.equal(confirm.status,200);await page.evaluate(value=>window.token=value,confirm.body.login.accessToken);
 const mfa=await login();assert.equal(mfa.status,200);assert.equal(mfa.body.mfaRequired,false);assert.ok(mfa.body.accessToken);
 await page.evaluate(value=>window.token=value,mfa.body.accessToken);
 assert.equal((await run(()=>call('passkeys',null,token,'GET'))).status,200);
 const passwordLogin=await run(()=>call('login',{email:'passkey-test@example.invalid',password:'Browser-fixture12!'}));
 assert.equal(passwordLogin.status,200);assert.equal(passwordLogin.body.mfaRequired,true);assert.equal(passwordLogin.body.accessToken,null);
 // The original non-MFA token remains rejected: this is not a blanket JWT-filter bypass.
 const stale=await page.evaluate(value=>call('passkeys',null,value,'GET'),process.env.AGORA_PASSKEY_TOKEN);assert.equal(stale.status,401);
 assert.equal((await run(()=>call('passkeys/'+keyId,null,token,'DELETE'))).status,200);
 const removed=await login();assert.equal(removed.status,400);
 console.log('PASS real WebAuthn registration/assertion, durable public key, cookie binding, UV required, signature/origin/replay rejection, account ownership, revocation and direct passkey login under enabled MFA');
}finally{await browser.close()}
