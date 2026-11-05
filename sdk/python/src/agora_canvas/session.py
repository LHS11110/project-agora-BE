from __future__ import annotations
import copy
import json
import math
import threading
import time
import uuid
from collections import deque, OrderedDict
from urllib.parse import urlencode
from websockets.sync.client import connect
from websockets.exceptions import ConnectionClosed
from .errors import AgoraError, Disconnected, OutcomeUnknown
from .presence import PresenceConnection

TRANSIENT={"BROKER_BUSY","BROKER_QUEUE_FULL","RATE_LIMITED","BROKER_BACKEND_UNAVAILABLE","QUERY_OUTCOME_UNKNOWN","QUERY_UNAVAILABLE","STORAGE_UNAVAILABLE","CANVAS_WRITE_CONFLICT"}

class CanvasSession:
    """One authenticated participant, ordered durable mutations and a live local snapshot.

    A background reader handles broadcasts and reconnects. One mutation is in flight;
    retries retain the exact client ID, request ID, sequence and operation payload.
    """
    def __init__(self,client,canvas_id,*,password=None,timeout=30):
        if canvas_id<=0:raise ValueError("Expected a positive canvas ID")
        self.client,self.canvas_id,self.timeout=client,canvas_id,timeout
        self._password=password;self._password_grant=None
        self.client_id=str(uuid.uuid4());self.revision=0;self.capabilities={}
        self._items={};self._count=0;self._user_id=None;self._socket=None
        self._cv=threading.Condition(threading.RLock());self._send_lock=threading.RLock();self._mutation_lock=threading.Lock();self._rpc_lock=threading.Lock()
        self._closed=threading.Event();self._thread=None;self._ready=False;self._terminal=None
        self._flight=None;self._flight_error=None;self._sent_at=0;self._waiters={};self._events=deque(maxlen=256)
        self._realtime_events=deque(maxlen=256);self._cursor_events=OrderedDict();self._event_overflow=0
        self._presence=PresenceConnection(client,canvas_id,self.client_id,timeout,self._closed,self._presence_event,self._presence_lost)
    def __repr__(self):return f"CanvasSession(canvas_id={self.canvas_id}, connected={self._ready})"
    def __enter__(self):return self.connect()
    def __exit__(self,*args):self.close()
    @property
    def items(self):
        with self._cv:return copy.deepcopy(self._items)
    @property
    def connected(self):return self._ready and not self._closed.is_set()
    @property
    def presence_connected(self):return self._presence.connected
    @property
    def peer_id(self):return self._presence.peer_id
    @property
    def peers(self):return self._presence.peers
    def _presence_lost(self):
        with self._cv:self._ready=False;self._cv.notify_all()
        if self._socket:self._socket.close()
    def _presence_event(self,event):
        if event.get('type')=='canvas_access_revoked':self._handle(event);return
        with self._cv:
            value=copy.deepcopy(event)
            if event.get('type')=='canvas_relay' and event.get('payload',{}).get('type')=='cursor':
                key=event.get('peer',{}).get('peer_id')
                self._cursor_events[key]=value
                if len(self._cursor_events)>1024:self._cursor_events.popitem(last=False)
            else:self._realtime_events.append(value)
            if event.get('type')=='rtc_peer_left':self._cursor_events.pop(event.get('peer_id'),None)
            self._cv.notify_all()
    def connect(self):
        if self._closed.is_set():raise Disconnected("Session is closed")
        if self._thread:return self
        self._open()
        self._thread=threading.Thread(target=self._read_loop,name=f"agora-canvas-{self.canvas_id}",daemon=True);self._thread.start()
        return self
    def close(self):
        self._closed.set();self._ready=False
        self._presence.close()
        if self._socket:self._socket.close()
        with self._cv:self._cv.notify_all()
        if self._thread and self._thread is not threading.current_thread():self._thread.join(timeout=5)
    def _open(self):
        body={}
        if self._password_grant:body["canvas_password_token"]=self._password_grant
        elif self._password is not None:body["password"]=self._password
        access=self.client.request("POST",f"/api/canvases/{self.canvas_id}/access",json=body)
        self._password_grant=access.get("canvas_password_token") or self._password_grant
        query=urlencode({"token":access["canvas_access_token"],"client_id":self.client_id})
        uri=f"{self.client.ws_origin.rstrip('/')}/wss/port/{access['ws_port']}/canvas/{self.canvas_id}?{query}"
        try:
            sock=connect(uri,ssl=self.client.ssl_context,origin=self.client.base_url,open_timeout=self.timeout,close_timeout=3,max_size=16*1024*1024,max_queue=64,proxy=None)
            self._socket=sock
            if self._closed.is_set():sock.close();raise Disconnected("Session is closed")
            deadline=time.monotonic()+self.timeout
            while time.monotonic()<deadline:
                event=json.loads(sock.recv(timeout=max(.1,deadline-time.monotonic())))
                if event.get("type")=="init_items":
                    self._handle(event)
                    self._presence.open(access,event)
                    return
                if event.get("type")=="error":raise AgoraError("Canvas join rejected",code=event.get("code"))
            raise Disconnected("Canvas snapshot timed out")
        except AgoraError:
            self._presence.close();self._ready=False
            if self._socket:self._socket.close()
            raise
        except Exception:
            self._presence.close();self._ready=False
            if self._socket:self._socket.close()
            raise Disconnected("WSS canvas connection failed") from None
    def _send(self,event):
        with self._send_lock:
            if not self._socket:raise Disconnected("Canvas is not connected")
            self._socket.send(json.dumps(event,ensure_ascii=False,allow_nan=False,separators=(",",":")))
    def _sync(self):self._send({"type":"canvas_sync","client_id":self.client_id,"request_id":str(uuid.uuid4())})
    def _ack(self,count):
        self._count=max(self._count,count)
        if self._flight and count>=self._flight["request_count"]:self._flight=None;self._flight_error=None
    def _handle(self,event):
        kind=event.get("type");need_sync=False
        with self._cv:
            if kind in ("init_items","canvas_snapshot"):
                if event.get("sync_protocol")!=2 or not isinstance(event.get("items"),dict) or type(event.get("revision")) is not int or type(event.get("accepted_count")) is not int:raise AgoraError("Unsupported canvas snapshot",code="PROTOCOL_INVALID")
                if event.get("client_id") not in (None,self.client_id):return
                if self._user_id is not None and self._user_id!=event.get("sync_user_id"):raise AgoraError("Participant identity changed",code="PROTOCOL_INVALID")
                self._user_id=event.get("sync_user_id")
                if event["revision"]>=self.revision and event["accepted_count"]>=self._count:
                    self._items={k:v for k,v in event["items"].items() if v and v.get("kind")!="host-tombstone"}
                    self.revision=event["revision"];self._ack(event["accepted_count"]);self._ready=True
                if kind=="init_items":self.capabilities={k:copy.deepcopy(event.get(k)) for k in ("groups","member_groups","write_groups","execute_groups","is_admin","group_bits","group_masks")}
            elif kind=="canvas_access_changed":
                self.capabilities={k:copy.deepcopy(event.get(k)) for k in ("groups","member_groups","write_groups","execute_groups","is_admin","group_bits","group_masks")}
            elif kind=="canvas_access_revoked":
                self._items={};self._ready=False
                self._terminal=AgoraError("Canvas access revoked",code=event.get("code","CANVAS_ACCESS_UNAUTHORIZED"))
                self._closed.set()
            elif kind=="canvas_commit":
                if event.get("revision")==self.revision+1 and self._ready:
                    for key,value in event.get("items",{}).items():
                        if value is None:self._items.pop(key,None)
                        else:self._items[key]=value
                    for move in event.get("translations",[]):
                        for key in move["ids"]:
                            item=self._items.get(key)
                            if not item:continue
                            if item.get("kind")=="stroke":
                                for p in item.get("points",[]):p["x"]+=move["dx"];p["y"]+=move["dy"]
                            else:item["x"]=item.get("x",0)+move["dx"];item["y"]=item.get("y",0)+move["dy"]
                    self.revision=event["revision"]
                    if event.get("client_id")==self.client_id and event.get("author_id")==self._user_id:self._ack(event["request_count"])
                elif event.get("revision",0)>self.revision:self._ready=False;need_sync=True
            elif kind=="canvas_watermark" and (event.get("force_sync") or event.get("revision",0)>self.revision):self._ready=False;need_sync=True
            elif kind in ("canvas_result","error") and self._flight and event.get("request_id")==self._flight["request_id"]:
                if event.get("ok"):
                    if event.get("duplicate"):need_sync=True
                elif event.get("code") in TRANSIENT:pass
                elif event.get("code")=="CLIENT_SEQUENCE_CONFLICT":self._ready=False;need_sync=True
                else:self._flight_error=AgoraError("Canvas mutation rejected: "+str(event.get("code","UNKNOWN")),code=event.get("code"));self._flight=None
            request_id=event.get("request_id")
            if request_id in self._waiters:self._waiters[request_id]=event
            # Large snapshots are available through .items rather than duplicated in the event queue.
            summary={"type":kind,"revision":self.revision}
            if kind=="canvas_commit":
                summary["changed_ids"]=list(dict.fromkeys([*event.get("items",{}),*(key for move in event.get("translations",[]) for key in move["ids"])]));summary["translations"]=event.get("translations",[])
                for key in ("author_id","client_id","request_id","request_count"):
                    if key in event:summary[key]=event[key]
            if kind not in ("canvas_watermark","pong","canvas_result"):
                if len(self._events)==self._events.maxlen:self._event_overflow+=1
                self._events.append(summary if kind in ("init_items","canvas_snapshot","canvas_commit") else event)
            self._cv.notify_all()
        if need_sync:self._sync()
    def _read_loop(self):
        synced=time.monotonic()
        while not self._closed.is_set():
            try:
                try:
                    event=json.loads(self._socket.recv(timeout=1))
                    if event.get("type")=="server_reconnect":raise Disconnected("Server requested reconnect")
                    self._handle(event)
                except TimeoutError:pass
                now=time.monotonic()
                with self._cv:flight=copy.deepcopy(self._flight) if self._ready else None
                if flight and now-self._sent_at>=1:self._send(flight);self._sent_at=now
                if now-synced>=10:
                    if self._ready:self._send({"type":"ping"})
                    else:self._sync()
                    synced=now
            except Exception:
                if self._closed.is_set():break
                with self._cv:
                    self._ready=False
                    for key,value in self._waiters.items():
                        if value is None:self._waiters[key]=Disconnected("Connection changed before the response; inspect current state before retrying")
                    self._cv.notify_all()
                self._presence.close()
                if self._socket:self._socket.close()
                delay=.2
                while not self._closed.wait(delay):
                    try:self._open();self._sent_at=0;break
                    except AgoraError as error:
                        if error.status in (401,403,404) or error.code in ("PROTOCOL_INVALID",):
                            with self._cv:self._terminal=error;self._closed.set();self._cv.notify_all()
                            return
                    delay=min(delay*2,10)
        self._presence.close()
        if self._socket:self._socket.close()
        self._ready=False
        with self._cv:self._cv.notify_all()
    def _check(self):
        if self._terminal:raise self._terminal
        if self._closed.is_set():raise Disconnected("Canvas session is closed")
    def _wait(self,predicate,timeout=None):
        deadline=time.monotonic()+(self.timeout if timeout is None else timeout)
        while not predicate():
            self._check();remaining=deadline-time.monotonic()
            if remaining<=0:raise TimeoutError()
            self._cv.wait(remaining)
        self._check()
    def apply(self,operations,*,timeout=None):
        operations=json.loads(json.dumps(operations,allow_nan=False))
        if isinstance(operations,list):
            for op in operations:
                if not isinstance(op,dict):raise ValueError("Operations must be objects")
                patch=op.get("patch",{})
                previous=self._items.get(str(op.get("id")),{})
                kind=patch.get("kind",previous.get("kind"))
                field="code" if kind=="code" else "text"
                if kind in ("text","note","code") and field in patch and "automerge_snapshot" not in patch and "automerge_changes" not in patch:
                    op["unset"]=list(dict.fromkeys([*op.get("unset",[]),"automerge_snapshot","automerge_changes"]))
        if not isinstance(operations,list) or not 1<=len(operations)<=512:raise ValueError("A batch must contain 1..512 operations")
        if len(json.dumps(operations,ensure_ascii=False).encode())>8*1024*1024:raise ValueError("Batch exceeds 8 MiB")
        with self._mutation_lock,self._cv:
            try:self._wait(lambda:self._ready and self._flight is None,timeout)
            except TimeoutError:raise OutcomeUnknown("Previous mutation or reconnect is still pending") from None
            self._flight_error=None
            self._flight={"type":"canvas_ops","client_id":self.client_id,"request_count":self._count+1,"request_id":str(uuid.uuid4()),"operations":operations}
            try:self._send(self._flight);self._sent_at=time.monotonic()
            except (ConnectionClosed,OSError):self._ready=False
            # Reader retries this exact envelope until commit or an authoritative receipt.
            try:self._wait(lambda:self._flight is None,timeout)
            except TimeoutError:raise OutcomeUnknown("Mutation receipt is pending; call wait_for_idle(), don't resubmit as a new edit") from None
            if self._flight_error:raise self._flight_error
            return self.revision
    def wait_for_idle(self,timeout=None):
        with self._cv:
            self._wait(lambda:self._flight is None and self._ready,timeout)
            if self._flight_error:raise self._flight_error
            return self.revision
    def create(self,item,*,id=None):
        key=str(id) if id is not None else str(uuid.uuid4());item=copy.deepcopy(item)
        if "permission" not in item:
            groups=self.capabilities.get("member_groups") or self.capabilities.get("groups") or []
            item["permission"]=sorted(set(groups))
        self.apply([{"action":"create","id":key,"patch":item,"unset":[]}]);return key
    def patch(self,id,fields,*,unset=()):return self.apply([{"action":"patch","id":str(id),"patch":fields,"unset":list(unset)}])
    def set_text(self,id,text,*,code=False):return self.patch(id,{"code" if code else "text":text},unset=("automerge_snapshot","automerge_changes"))
    def delete(self,*ids,include_connections=True):
        keys=set(map(str,ids))
        if include_connections:
            with self._cv:keys.update(key for key,item in self._items.items() if item.get("kind")=="connector" and (str(item.get("from")) in keys or str(item.get("to")) in keys))
        return self.apply([{"action":"delete","id":key} for key in sorted(keys)])
    def move(self,ids,dx,dy):return self.apply([{"action":"translate","ids":[str(id) for id in ids],"dx":dx,"dy":dy}])
    def lock(self,id,locked=True):return self.patch(id,{"locked":bool(locked)})
    def _request(self,event,*,timeout=None):
        with self._rpc_lock:
            request_id=str(uuid.uuid4());event={**event,"request_id":request_id}
            with self._cv:
                self._wait(lambda:self._ready,timeout);self._waiters[request_id]=None
            try:
                self._send(event)
                with self._cv:
                    self._wait(lambda:self._waiters[request_id] is not None,timeout);reply=self._waiters[request_id]
                if isinstance(reply,Exception):raise reply
                if reply.get("ok") is False or reply.get("type")=="error":raise AgoraError("Canvas request rejected",code=reply.get("code"))
                return reply
            finally:
                with self._cv:self._waiters.pop(request_id,None)
    def refresh(self):self._request({"type":"canvas_sync","client_id":self.client_id});return self.items
    def settings(self):return self._request({"type":"canvas_settings_get"})["settings"]
    def update_settings(self,field,*,value=None,**fields):
        settings=self.settings()
        return self._request({"type":"canvas_settings_update","field":field,"expected_revision":settings["settings_revision"],"value":value,**fields})["settings"]
    def invite(self,nickname,tag_number):return self.update_settings("participant_add",nickname=nickname,tag_number=tag_number)
    def remove_participant(self,nickname,tag_number):return self.update_settings("participant_remove",nickname=nickname,tag_number=tag_number)
    def create_bot(self,name,*,token_file=None):
        if token_file is not None:
            from pathlib import Path
            destination=Path(token_file).expanduser()
            if destination.exists() or destination.is_symlink():raise FileExistsError("Credential file already exists")
        created=self.client.request("POST",f"/api/canvases/{self.canvas_id}/bots",json={"name":name})
        bot=created["bot"]
        try:
            self.invite(bot["nickname"],bot["tag_number"])
            if token_file is not None:
                from .credentials import save_bot_credentials
                save_bot_credentials(token_file,created,base_url=self.client.base_url)
        except Exception:
            try:self.client.revoke_bot(self.canvas_id,bot["user_id"])
            except Exception:pass
            raise
        return created
    def revoke_bot(self,bot_id):
        bot=next((b for b in self.client.bots(self.canvas_id) if b["user_id"]==bot_id),None)
        if bot:
            participants=self.settings()["participants"]
            if any(p["nickname"]==bot["nickname"] and p["tag_number"]==bot["tag_number"] for p in participants):self.remove_participant(bot["nickname"],bot["tag_number"])
        return self.client.revoke_bot(self.canvas_id,bot_id)
    def execute(self,language,code,*,stdin=None):
        body={"language":language,"code":code,"passwordGrant":self._password_grant}
        if stdin is not None:body["stdin"]=stdin
        return self.client.request("POST",f"/api/canvases/{self.canvas_id}/runtime/execute",json=body)
    @staticmethod
    def _coordinate(value):
        if isinstance(value,bool) or not isinstance(value,(int,float)) or not math.isfinite(value):raise ValueError('Expected finite canvas coordinates')
        return value
    def send_presence(self,payload,*,peer_ids=None):
        if not isinstance(payload,dict):raise ValueError('Expected a presence payload')
        if payload.get('type')=='editor_presence':
            key=str(payload.get('item_id',''))
            with self._cv:
                item=self._items.get(key)
                if not item:raise AgoraError('Object is not visible',code='ITEM_ACCESS_DENIED')
                permission=item.get('permission',[])
                groups={permission} if isinstance(permission,str) else set(permission) if isinstance(permission,list) else {g for g,v in permission.items() if v is True or isinstance(v,(int,float)) and v>0} if isinstance(permission,dict) else set()
                if payload.get('editing') and not self.capabilities.get('is_admin') and not groups.intersection(self.capabilities.get('write_groups') or []):raise AgoraError('Object is read-only',code='ITEM_WRITE_DENIED')
                if payload.get('editing') and item.get('locked'):raise AgoraError('Object is locked',code='ITEM_LOCKED')
            permitted={p['peer_id'] for p in self.peers if p.get('is_admin') or groups.intersection(p.get('groups') or [])}
            peer_ids=list(permitted if peer_ids is None else permitted.intersection(peer_ids))
        return self._presence.relay(payload,peer_ids)
    def send_cursor(self,x=None,y=None,*,visible=True,selection=None):
        payload={'type':'cursor','visible':bool(visible)}
        if visible:
            payload.update(x=self._coordinate(x),y=self._coordinate(y),selection=selection)
        return self.send_presence(payload)
    def send_laser(self,phase,x,y,*,stroke_id=None,sequence=0):
        if phase not in ('start','point','end') or type(sequence) is not int or not 0<=sequence<9007199254740991:raise ValueError('Invalid laser phase or sequence')
        if stroke_id is None:
            if phase!='start':raise ValueError('stroke_id is required after start')
            stroke_id=str(uuid.uuid4())
        if not isinstance(stroke_id,str) or not 1<=len(stroke_id)<=128:raise ValueError('Invalid laser stroke ID')
        self.send_presence({'type':'laser','phase':phase,'stroke_id':stroke_id,'sequence':sequence,'x':self._coordinate(x),'y':self._coordinate(y)})
        return stroke_id
    def set_editing(self,item_id,editing=True):return self.send_presence({'type':'editor_presence','item_id':str(item_id),'editing':bool(editing)})
    def publish(self,event):
        kind=event.get('type')
        if kind in ('cursor','laser','editor_presence'):return self.send_presence(event)
        if kind=='canvas_relay':return self.send_presence(event.get('payload'),peer_ids=event.get('peer_ids'))
        if kind in ('rtc_list','rtc_signal'):return self._presence.send_control(event)
        with self._cv:self._wait(lambda:self._ready)
        self._send(event)
    def send_event(self,event):
        """Other existing broker APIs (chat, comments, mentions, cursors, etc.)."""
        if event.get('type') in ('cursor','laser','editor_presence','canvas_relay','rtc_list','rtc_signal'):return self.publish(event)
        return self._request(event)
    def next_event(self,timeout=None):
        with self._cv:
            try:self._wait(lambda:bool(self._events or self._realtime_events or self._cursor_events or self._event_overflow),timeout)
            except TimeoutError:return None
            if self._event_overflow:
                count=self._event_overflow;self._event_overflow=0
                return {'type':'event_overflow','dropped':count,'revision':self.revision}
            if self._events:return self._events.popleft()
            if self._realtime_events:return self._realtime_events.popleft()
            return self._cursor_events.popitem(last=False)[1]
