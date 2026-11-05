import json
import ssl
import subprocess
import tempfile
import threading
import time
import unittest
from pathlib import Path
from urllib.parse import urlsplit,parse_qs
from websockets.sync.server import serve
from agora_canvas import AgoraClient,CanvasSession,AgoraError


def wait_for(predicate,timeout=10):
    end=time.monotonic()+timeout
    while not predicate():
        if time.monotonic()>end:raise AssertionError('Timed out waiting for presence state')
        time.sleep(.02)


class PresenceUnitTests(unittest.TestCase):
    def session(self):
        session=CanvasSession(None,42,timeout=.02)
        session._handle({'type':'init_items','sync_protocol':2,'revision':0,'accepted_count':0,'sync_user_id':7,'items':{},'groups':['private'],'member_groups':['private'],'write_groups':['private'],'is_admin':False})
        self.sent=[]
        class Socket:
            def send(_,wire):self.sent.append(json.loads(wire))
            def close(_):pass
        session._presence._socket=Socket()
        session._presence._handle({'type':'rtc_peers','self_peer_id':'self','peers':[{'peer_id':'same','groups':['private']},{'peer_id':'other','groups':['other']},{'peer_id':'admin','is_admin':True}]})
        self.addCleanup(session.close)
        return session

    def test_helpers_use_presence_transport_and_filter_object_presence(self):
        session=self.session();session._items['text']={'kind':'text','permission':['private']}
        session.send_cursor(.1,-.2,selection={'start':{'x':0,'y':0},'end':{'x':1,'y':1}})
        self.assertEqual(set(self.sent[-1]['peer_ids']),{'same','other','admin'})
        session.set_editing('text')
        self.assertEqual(set(self.sent[-1]['peer_ids']),{'same','admin'})
        laser=session.send_laser('start',.1,.2)
        session.send_laser('end',.2,.3,stroke_id=laser,sequence=1)
        self.assertEqual(self.sent[-1]['payload']['stroke_id'],laser)
        self.assertEqual(self.sent[-1]['payload']['phase'],'end')
        session.send_cursor(visible=False)
        self.assertEqual(self.sent[-1]['payload'],{'type':'cursor','visible':False})
        session.capabilities['write_groups']=[]
        with self.assertRaises(AgoraError) as error:session.set_editing('text')
        self.assertEqual(error.exception.code,'ITEM_WRITE_DENIED')
        with self.assertRaises(ValueError):session.send_cursor(float('nan'),0)
        with self.assertRaises(ValueError):session.send_presence({'type':'arbitrary'})

    def test_cursor_flood_does_not_discard_object_changes_and_peer_copy_is_independent(self):
        session=self.session()
        session._handle({'type':'canvas_commit','revision':1,'author_id':9,'client_id':'remote','items':{'a':{'kind':'shape'}}})
        for i in range(2000):session._presence_event({'type':'canvas_relay','peer':{'peer_id':'same'},'payload':{'type':'cursor','x':i,'y':0}})
        self.assertEqual(len(session._cursor_events),1)
        self.assertEqual(len(session._events),2)
        session.next_event();event=session.next_event()
        self.assertEqual((event['type'],event['author_id'],event['changed_ids']),('canvas_commit',9,['a']))
        session.next_event() # initial rtc_peers
        self.assertEqual(session.next_event()['payload']['x'],1999)
        peers=session.peers;peers[0]['groups']=[]
        self.assertEqual(session.peers[0]['groups'],['private'])

    def test_missing_binding_and_unknown_targets_are_rejected(self):
        session=self.session()
        with self.assertRaises(AgoraError) as error:session._presence.open({}, {})
        self.assertEqual(error.exception.code,'PROTOCOL_INVALID')
        session=self.session()
        with self.assertRaises(ValueError):session.send_presence({'type':'cursor','x':0,'y':0},peer_ids=['foreign'])

    def test_event_overflow_is_explicit_and_latest_snapshot_is_retained(self):
        session=self.session()
        for revision in range(1,300):session._handle({'type':'canvas_commit','revision':revision,'items':{'a':{'kind':'shape','x':revision}}})
        event=session.next_event()
        self.assertEqual(event['type'],'event_overflow');self.assertGreater(event['dropped'],0)
        self.assertEqual(session.items['a']['x'],299)


class PresenceNetworkTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.directory=tempfile.TemporaryDirectory(prefix='agora-presence-tls-')
        cls.cert=Path(cls.directory.name)/'cert.pem';cls.key=Path(cls.directory.name)/'key.pem'
        subprocess.run(['openssl','req','-x509','-newkey','rsa:2048','-nodes','-days','1','-keyout',str(cls.key),'-out',str(cls.cert),'-subj','/CN=127.0.0.1','-addext','subjectAltName=IP:127.0.0.1'],check=True,capture_output=True)
    @classmethod
    def tearDownClass(cls):cls.directory.cleanup()
    def setUp(self):
        self.lock=threading.RLock();self.data={};self.bindings={};self.presence={};self.generation=0;self.relayed=[];self.verified=0
        tls=ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER);tls.load_cert_chain(self.cert,self.key)
        self.server=serve(self.handler,'127.0.0.1',0,ssl=tls)
        self.port=self.server.socket.getsockname()[1]
        self.thread=threading.Thread(target=self.server.serve_forever,daemon=True);self.thread.start()
        self.clients=[]
    def tearDown(self):
        for client in self.clients:client.close()
        self.server.shutdown();self.thread.join(3)
    def send(self,sock,event):sock.send(json.dumps(event))
    def public(self,peer):return {'peer_id':peer,'nickname':'participant','tag_number':1,'groups':['default'],'write_groups':['default'],'is_admin':False}
    def handler(self,sock):
        query=parse_qs(urlsplit(sock.request.path).query);client=query['client_id'][0]
        rtc='/rtc/' in sock.request.path
        try:
            if not rtc:
                with self.lock:
                    self.generation+=1;connection=f'data-{self.generation}';proof=f'proof-{self.generation}';self.data[client]=sock;self.bindings[client]=(connection,proof)
                self.send(sock,{'type':'init_items','sync_protocol':2,'sync_user_id':7,'revision':0,'accepted_count':0,'client_id':client,'items':{},'groups':['default'],'write_groups':['default'],'rtc_canvas_connection_id':connection,'rtc_canvas_connection_hash':proof})
                for raw in sock:
                    event=json.loads(raw)
                    if event.get('type')=='canvas_sync':self.send(sock,{'type':'canvas_snapshot','sync_protocol':2,'sync_user_id':7,'revision':0,'accepted_count':0,'client_id':client,'items':{}})
            else:
                self.send(sock,{'type':'rtc_ready'});join=json.loads(sock.recv(timeout=5))
                with self.lock:
                    assert (join['canvas_connection_id'],join['canvas_connection_hash'])==self.bindings[client]
                    assert join['canvas_protocol']==2 and join['canvas_transport']=='broker-v1'
                    peer='peer-'+join['canvas_connection_id'];existing=list(self.presence.items());self.presence[peer]=(sock,client);self.verified+=1
                    self.send(sock,{'type':'rtc_peers','self_peer_id':peer,'peers':[self.public(p) for p,_ in existing]})
                    for _,(other,_) in existing:self.send(other,{'type':'rtc_peer_joined','peer':self.public(peer)})
                for raw in sock:
                    event=json.loads(raw)
                    if event['type']=='rtc_list':
                        with self.lock:self.send(sock,{'type':'rtc_peers','self_peer_id':peer,'peers':[self.public(p) for p in self.presence if p!=peer]})
                    elif event['type']=='canvas_relay':
                        with self.lock:
                            self.relayed.append(event)
                            for target in event['peer_ids']:
                                if target in self.presence:self.send(self.presence[target][0],{'type':'canvas_relay','peer':self.public(peer),'payload':event['payload']})
        except Exception:pass
        finally:
            if rtc and 'peer' in locals():
                with self.lock:
                    self.presence.pop(peer,None)
                    for other,_ in list(self.presence.values()):
                        try:self.send(other,{'type':'rtc_peer_left','peer_id':peer})
                        except Exception:pass
    def session(self):
        client=AgoraClient(ca_file=str(self.cert),ws_origin=f'wss://127.0.0.1:{self.port}')
        client.request=lambda *args,**kwargs:{'ws_port':8002,'canvas_access_token':'test-access'}
        self.clients.append(client)
        return client.canvas(42,timeout=5).connect()
    def test_two_tls_connections_are_bound_and_presence_relays_bidirectionally(self):
        a=self.session();b=self.session();wait_for(lambda:len(a.peers)==len(b.peers)==1)
        self.assertEqual(self.verified,2);self.assertTrue(a.presence_connected)
        a.send_cursor(.1,.2)
        def cursor():
            event=b.next_event(.1)
            return event and event.get('type')=='canvas_relay' and event['payload'].get('x')==.1
        wait_for(cursor)
        stroke=b.send_laser('start',.3,.4)
        wait_for(lambda:any(e['payload'].get('stroke_id')==stroke for e in self.relayed))
        a.close();wait_for(lambda:len(b.peers)==0)
    def test_presence_failure_reopens_both_channels_with_fresh_proof_and_no_cursor_replay(self):
        a=self.session();b=self.session();wait_for(lambda:len(a.peers)==1)
        peer=a.peer_id;a.send_cursor(.1,.2);wait_for(lambda:len(self.relayed)==1)
        with self.lock:sock=self.presence[peer][0]
        sock.close(code=1012)
        wait_for(lambda:a.presence_connected and a.peer_id!=peer)
        self.assertTrue(a.connected);self.assertGreaterEqual(self.verified,3)
        self.assertEqual(len(self.relayed),1)
        wait_for(lambda:len(b.peers)==1 and b.peers[0]['peer_id']==a.peer_id)
    def test_data_failure_rejoins_presence_without_losing_identity(self):
        a=self.session();peer=a.peer_id
        with self.lock:sock=self.data[a.client_id]
        self.send(sock,{'type':'server_reconnect','reason':'credentials_expired'})
        wait_for(lambda:a.presence_connected and a.peer_id!=peer)
        self.assertEqual(a._user_id,7);self.assertTrue(a.connected)
