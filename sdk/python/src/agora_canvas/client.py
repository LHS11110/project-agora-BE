from __future__ import annotations
import ssl
import weakref
from urllib.parse import urlsplit
import httpx
from .errors import AgoraError, MFARequired

class AgoraClient:
    """Authenticated HTTP client. TLS verification is always enabled."""
    def __init__(self, base_url="https://frelog.p-e.kr", *, token=None, ca_file=None, timeout=30, ws_origin=None):
        parsed=urlsplit(base_url)
        if parsed.scheme!="https" or not parsed.hostname or parsed.username or parsed.password or parsed.query or parsed.fragment or parsed.path not in ("", "/"):
            raise ValueError("base_url must be an HTTPS origin")
        self.base_url=base_url.rstrip("/")
        self.ws_origin=ws_origin or self.base_url.replace("https://","wss://",1)
        ws=urlsplit(self.ws_origin)
        if ws.scheme!="wss" or not ws.hostname or ws.username or ws.password or ws.path not in ("", "/") or ws.query or ws.fragment:raise ValueError("ws_origin must be a WSS origin")
        self.ssl_context=ssl.create_default_context(cafile=ca_file)
        # Identify HTTPS/WSS on Wall's shared HTTPS + TURN/TLS listener.
        self.ssl_context.set_alpn_protocols(["http/1.1"])
        self._sessions=weakref.WeakSet()
        self._token=token
        self.http=httpx.Client(base_url=self.base_url,verify=self.ssl_context,timeout=timeout,follow_redirects=False,trust_env=False)
    def __repr__(self):return f"AgoraClient({self.base_url!r}, authenticated={bool(self._token)})"
    def __enter__(self):return self
    def __exit__(self,*args):self.close()
    def close(self):
        for session in list(self._sessions):session.close()
        self.http.close()
    def request(self, method, path, *, raw=False, **kwargs):
        if not path.startswith("/api/") or ".." in path or "?" in path or "#" in path:raise ValueError("Expected an API path")
        headers={**kwargs.pop("headers",{})}
        if self._token:headers["Authorization"]="Bearer "+self._token
        try:response=self.http.request(method,path,headers=headers,**kwargs)
        except httpx.HTTPError:raise AgoraError("HTTPS request failed") from None
        try:data=response.json() if response.content else None
        except ValueError:data=None
        if response.is_error:raise AgoraError((data or {}).get("message","API request failed") if isinstance(data,dict) else "API request failed",status=response.status_code,code=data.get("code") if isinstance(data,dict) else None)
        return response if raw else data
    def login(self,email,password,*,mfa_code=None):
        data=self.request("POST","/api/auth/login",json={"email":email,"password":password})
        if data.get("mfaRequired"):
            if mfa_code is None:raise MFARequired(data["challengeToken"])
            return self.complete_mfa(data["challengeToken"],mfa_code)
        self._token=data["accessToken"];return data["user"]
    def complete_mfa(self,challenge,code):
        data=self.request("POST","/api/auth/mfa/verify",json={"challengeToken":challenge,"code":code})
        self._token=data["accessToken"];return data["user"]
    @classmethod
    def from_bot(cls,token,**kwargs):
        if not token.startswith("agora_bot_"):raise ValueError("Expected a bot credential")
        return cls(token=token,**kwargs)
    @classmethod
    def from_bot_file(cls,path,**kwargs):
        from .credentials import load_bot_credentials
        saved=load_bot_credentials(path)
        kwargs.setdefault("base_url",saved["base_url"])
        client=cls.from_bot(saved["token"],**kwargs)
        client.bot_canvas_id=saved["bot"]["canvas_id"]
        return client
    def me(self):return self.request("GET","/api/auth/me")
    def canvases(self,name=None):return self.request("GET","/api/canvases",params={"name":name} if name is not None else None)
    def create_canvas(self,name,*,description="",visibility="private",password=None):
        return self.request("POST","/api/canvases",json={"canvasName":name,"description":description,"visibility":visibility,"canvasPassword":password})
    def delete_canvas(self,canvas_id):return self.request("DELETE",f"/api/canvases/{int(canvas_id)}")
    def canvas(self,canvas_id,*,password=None,timeout=30):
        from .session import CanvasSession
        session=CanvasSession(self,int(canvas_id),password=password,timeout=timeout)
        self._sessions.add(session)
        return session
    def bots(self,canvas_id):return self.request("GET",f"/api/canvases/{int(canvas_id)}/bots")
    def revoke_bot(self,canvas_id,bot_id):return self.request("DELETE",f"/api/canvases/{int(canvas_id)}/bots/{int(bot_id)}")
    def rotate_bot(self,canvas_id,bot_id):return self.request("POST",f"/api/canvases/{int(canvas_id)}/bots/{int(bot_id)}/rotate")
