"""Authenticated broker presence transport. No WebRTC/media connection is created."""
from __future__ import annotations
import copy
import json
import threading
import time
import uuid
from urllib.parse import urlencode
from websockets.sync.client import connect
from .errors import AgoraError, Disconnected


class PresenceConnection:
    def __init__(self, client, canvas_id, client_id, timeout, closed, receive, lost):
        self.client, self.canvas_id, self.client_id, self.timeout = client, canvas_id, client_id, timeout
        self.closed, self.receive, self.lost = closed, receive, lost
        self._lock = threading.RLock()
        self._socket = None
        self._thread = None
        self._stop = threading.Event()
        self._ready = False
        self._peer_id = ''
        self._peers = {}

    @property
    def connected(self):
        with self._lock: return self._ready and not self.closed.is_set()

    @property
    def peer_id(self):
        with self._lock: return self._peer_id

    @property
    def peers(self):
        with self._lock: return copy.deepcopy(list(self._peers.values()))

    def close(self):
        self._stop.set()
        with self._lock:
            sock, thread = self._socket, self._thread
            self._socket = None
            self._ready = False
            self._peer_id = ''
            self._peers.clear()
        if sock: sock.close()
        if thread and thread is not threading.current_thread(): thread.join(timeout=3)
        self._thread = None

    def open(self, access, initial):
        self.close()
        proof = initial.get('rtc_canvas_connection_hash')
        connection = initial.get('rtc_canvas_connection_id')
        if not isinstance(proof, str) or not proof or not isinstance(connection, str) or not connection:
            raise AgoraError('Missing authenticated presence binding', code='PROTOCOL_INVALID')
        query = urlencode({'token':access['canvas_access_token'], 'client_id':self.client_id})
        uri = f"{self.client.ws_origin}/wss/port/{access['ws_port']}/rtc/canvas/{self.canvas_id}?{query}"
        sock = connect(uri, ssl=self.client.ssl_context, origin=self.client.base_url,
                       open_timeout=self.timeout, close_timeout=3, max_size=16*1024*1024, max_queue=64, proxy=None)
        stop = threading.Event()
        with self._lock: self._socket, self._stop = sock, stop
        try:
            if self.closed.is_set(): raise Disconnected('Session is closed')
            deadline = time.monotonic() + self.timeout
            while time.monotonic() < deadline:
                event = json.loads(sock.recv(timeout=max(.1, deadline-time.monotonic())))
                if event.get('type') == 'rtc_ready':
                    self._send({'type':'rtc_join', 'request_id':str(uuid.uuid4()), 'canvas_protocol':2,
                                'canvas_transport':'broker-v1', 'canvas_connection_id':connection,
                                'canvas_connection_hash':proof})
                elif event.get('type') == 'rtc_peers':
                    self._handle(event)
                    break
                elif event.get('type') in ('error','server_reconnect','canvas_access_revoked'):
                    raise AgoraError('Presence join rejected', code=event.get('code','RTC_JOIN_FAILED'))
            if not self.connected: raise Disconnected('Presence join timed out')
            self._thread = threading.Thread(target=self._read, args=(sock,stop),
                                            name=f'agora-presence-{self.canvas_id}', daemon=True)
            self._thread.start()
        except BaseException:
            self.close()
            raise

    def _send(self, event):
        wire = json.dumps(event, ensure_ascii=False, allow_nan=False, separators=(',',':'))
        with self._lock:
            if self._socket is None: raise Disconnected('Presence is not connected')
            self._socket.send(wire)

    def send_control(self, event):
        if event.get('type') not in ('rtc_list','rtc_signal'):
            raise ValueError('Unsupported presence control event')
        if not self.connected: raise Disconnected('Presence is not connected')
        self._send(event)

    def relay(self, payload, peer_ids=None):
        if not isinstance(payload,dict) or payload.get('type') not in ('cursor','laser','editor_presence'):
            raise ValueError('Expected cursor, laser or editor_presence')
        if len(json.dumps(payload,ensure_ascii=False,allow_nan=False).encode()) > 65536:
            raise ValueError('Presence payload exceeds 64 KiB')
        with self._lock:
            if not self._ready or self.closed.is_set(): raise Disconnected('Presence is not connected')
            targets = list(self._peers) if peer_ids is None else list(dict.fromkeys(peer_ids))
            if len(targets)>512 or any(not isinstance(p,str) or p not in self._peers or p==self._peer_id for p in targets):
                raise ValueError('Expected current peer IDs from this canvas')
            if not targets: return 0
            self._send({'type':'canvas_relay','request_id':str(uuid.uuid4()),'peer_ids':targets,'payload':payload})
        return len(targets)

    def _handle(self,event):
        kind = event.get('type')
        with self._lock:
            if kind == 'rtc_peers':
                if not isinstance(event.get('self_peer_id'),str) or not isinstance(event.get('peers'),list):
                    raise AgoraError('Invalid presence membership',code='PROTOCOL_INVALID')
                self._peer_id = event['self_peer_id']
                self._peers = {p['peer_id']:copy.deepcopy(p) for p in event['peers']
                               if isinstance(p,dict) and isinstance(p.get('peer_id'),str) and p['peer_id']!=self._peer_id}
                self._ready = True
            elif kind == 'rtc_peer_joined':
                peer = event.get('peer')
                if isinstance(peer,dict) and isinstance(peer.get('peer_id'),str) and peer['peer_id']!=self._peer_id:
                    self._peers[peer['peer_id']] = copy.deepcopy(peer)
            elif kind == 'rtc_peer_left':
                self._peers.pop(event.get('peer_id'),None)
            elif kind == 'canvas_relay':
                peer = event.get('peer')
                if not isinstance(peer,dict) or not isinstance(peer.get('peer_id'),str): return
                self._peers[peer['peer_id']] = copy.deepcopy(peer)
            elif kind == 'error' and event.get('code') == 'CANVAS_RELAY_TARGET_INVALID':
                # A participant may leave between membership lookup and send.
                # Refresh membership; ephemeral traffic is never replayed.
                self._send({'type':'rtc_list'})
        self.receive(event)

    def _read(self,sock,stop):
        refreshed = time.monotonic()
        try:
            while not stop.is_set() and not self.closed.is_set():
                try:
                    event = json.loads(sock.recv(timeout=1))
                    if event.get('type') in ('server_reconnect','rtc_disconnected'):
                        raise Disconnected('Presence connection changed')
                    self._handle(event)
                    if event.get('type') == 'canvas_access_revoked': break
                except TimeoutError: pass
                if time.monotonic()-refreshed >= 15:
                    self._send({'type':'rtc_list'}); refreshed=time.monotonic()
        except Exception:
            if not stop.is_set() and not self.closed.is_set():
                with self._lock: self._ready=False; self._peers.clear()
                self.receive({'type':'presence_disconnected'})
                self.lost()
        finally: sock.close()
