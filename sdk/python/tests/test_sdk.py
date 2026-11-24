import json,unittest
import httpx
from agora_canvas import AgoraClient,CanvasSession,AgoraError,MFARequired,OutcomeUnknown

class SessionTests(unittest.TestCase):
 def session(self):
  session=CanvasSession(None,12,timeout=.02)
  session._send=lambda event:None
  session._handle({'type':'init_items','sync_protocol':2,'revision':0,'accepted_count':0,'sync_user_id':7,'items':{'a':{'kind':'shape','x':1,'y':2}},'groups':['default'],'write_groups':['default'],'execute_groups':[]})
  return session
 def test_live_access_update_preserves_session_and_refreshes_capabilities(self):
  s=self.session();s._handle({'type':'canvas_access_changed','groups':['new'],'member_groups':['new'],'write_groups':[],'execute_groups':['new'],'is_admin':False,'settings':{'settings_revision':2}})
  self.assertTrue(s.connected);self.assertEqual(s.capabilities['groups'],['new']);self.assertEqual(s.capabilities['write_groups'],[]);self.assertIn('a',s.items)
 def test_access_revocation_clears_items_and_is_terminal(self):
  s=self.session();s._handle({'type':'canvas_access_revoked','code':'CANVAS_ACCESS_UNAUTHORIZED'})
  self.assertFalse(s.connected);self.assertEqual(s.items,{})
  with self.assertRaises(AgoraError):s._check()
 def test_create_defaults_to_all_memberships_including_read_only_roles(self):
  s=self.session();s.capabilities.update({'member_groups':['writers','readers'],'write_groups':['writers']});captured=[]
  s.apply=lambda ops:captured.extend(ops)
  s.create({'kind':'shape'},id='created');self.assertEqual(captured[0]['patch']['permission'],['readers','writers'])
 def test_bitmask_metadata_preserves_the_64th_group_exactly(self):
  s=self.session();s._handle({'type':'init_items','sync_protocol':2,'revision':0,'accepted_count':0,'sync_user_id':7,'items':{},'groups':['high'],'member_groups':['high'],'write_groups':[],'execute_groups':['high'],'group_bits':{'high':63},'group_masks':{'read':'8000000000000000','execute':'8000000000000000','write':'0000000000000000'}})
  self.assertEqual(s.capabilities['group_bits']['high'],63);self.assertEqual(int(s.capabilities['group_masks']['read'],16),1<<63)
 def test_commit_is_required_and_snapshot_receipt_recovers_unknown_outcome(self):
  s=self.session();sent=[]
  s._send=lambda event:(sent.append(event),s._handle({'type':'canvas_result','request_id':event['request_id'],'ok':True}))
  with self.assertRaises(OutcomeUnknown):s.patch('a',{'x':9})
  self.assertEqual(s.items['a']['x'],1);self.assertIsNotNone(s._flight)
  flight=s._flight
  s._handle({'type':'canvas_snapshot','sync_protocol':2,'revision':1,'accepted_count':1,'sync_user_id':7,'client_id':s.client_id,'items':{'a':{'kind':'shape','x':9,'y':2}}})
  self.assertEqual(s.wait_for_idle(),1);self.assertIsNone(s._flight);self.assertEqual(s.items['a']['x'],9)
  self.assertEqual(len(sent),1);self.assertEqual(flight['request_count'],1)
 def test_revision_gap_does_not_apply_unconfirmed_content(self):
  s=self.session();sent=[];s._send=sent.append
  s._handle({'type':'canvas_commit','revision':2,'items':{'a':{'x':99}}})
  self.assertEqual(s.items['a']['x'],1);self.assertFalse(s.connected);self.assertEqual(sent[-1]['type'],'canvas_sync')
 def test_move_delete_and_permission_rejection(self):
  s=self.session()
  def respond(event):
   if event['type']!='canvas_ops':return
   if event['operations'][0]['action']=='patch':s._handle({'type':'canvas_result','request_id':event['request_id'],'ok':False,'code':'ITEM_LOCKED'})
   else:s._handle({'type':'canvas_commit','client_id':s.client_id,'author_id':7,'request_count':event['request_count'],'revision':s.revision+1,'items':{'a':None}})
  s._send=respond
  with self.assertRaises(AgoraError) as e:s.patch('a',{'x':3})
  self.assertEqual(e.exception.code,'ITEM_LOCKED');self.assertEqual(s._count,0)
  s.delete('a');self.assertNotIn('a',s.items);self.assertEqual(s._count,1)
 def test_translations_update_strokes_and_snapshot_copy_is_independent(self):
  s=self.session();s._items['stroke']={'kind':'stroke','points':[{'x':1,'y':2}]}
  s._handle({'type':'canvas_commit','revision':1,'translations':[{'ids':['a','stroke'],'dx':3,'dy':4}]})
  self.assertEqual(s.items['a']['x'],4);self.assertEqual(s.items['stroke']['points'][0],{'x':4,'y':6})
  data=s.items;data['a']['x']=500;self.assertEqual(s.items['a']['x'],4)
 def test_invalid_batch_and_protocol_are_rejected(self):
  s=self.session()
  with self.assertRaises(ValueError):s.apply([])
  with self.assertRaises(ValueError):s.patch('a',{'x':float('nan')})
  with self.assertRaises(AgoraError):s._handle({'type':'canvas_snapshot','sync_protocol':1})
 def test_plain_text_replacement_explicitly_clears_old_crdt(self):
  s=self.session();s._items['note']={'kind':'note','text':'old','automerge_snapshot':'old-base','automerge_changes':[]};sent=[]
  def respond(event):
   sent.append(event);s._handle({'type':'canvas_commit','client_id':s.client_id,'author_id':7,'request_count':event['request_count'],'revision':1,'items':{'note':{'kind':'note','text':'new'}}})
  s._send=respond;s.patch('note',{'text':'new'})
  self.assertEqual(set(sent[0]['operations'][0]['unset']),{'automerge_snapshot','automerge_changes'});self.assertEqual(s.items['note']['text'],'new')
 def test_delete_includes_attached_connectors(self):
  s=self.session();s._items['wire']={'kind':'connector','from':'a','to':'other'};sent=[]
  def respond(event):
   sent.append(event);s._handle({'type':'canvas_commit','client_id':s.client_id,'author_id':7,'request_count':event['request_count'],'revision':1,'items':{'a':None,'wire':None}})
  s._send=respond;s.delete('a');self.assertEqual({op['id'] for op in sent[0]['operations']},{'a','wire'})
 def test_snapshot_search_and_version_checked_patch(self):
  s=self.session()
  s._items['doc']={'kind':'note','text':'Quarterly Plan','x':.25,'y':.4,'__broker_revision':4}
  matches=s.find_objects('PLAN',kind='note')
  self.assertEqual(set(matches),{'doc'})
  matches['doc']['x']=100
  self.assertEqual(s.items['doc']['x'],.25)
  sent=[]
  def respond(event):
   sent.append(event)
   s._handle({'type':'canvas_result','ok':False,'code':'CANVAS_WRITE_CONFLICT','request_id':event['request_id']})
  s._send=respond
  with self.assertRaises(AgoraError) as error:s.set_text('doc','new',expected_revision=4)
  self.assertEqual(error.exception.code,'CANVAS_WRITE_CONFLICT')
  self.assertIsNone(s._flight)
  self.assertEqual(sent[0]['operations'][0]['expected_revision'],4)
 def test_agent_only_completes_on_final_response(self):
  s=self.session()
  class Client:
   def agent_terms(self):return {'version':'server-version','accepted':True}
  s.client=Client()
  sent=[]
  def respond(event):
   sent.append(event)
   req=event['request_id']
   s._handle({'type':'runtime_output','request_id':req,'output':'planning'})
   self.assertIsNone(s._waiters[req])
   s._handle({'type':'runtime_result','request_id':req,'ok':True,'result':{'answer':'located','highlight_id':'a'}})
  s._send=respond
  result=s.run_agent('find a',selected_ids=['a'],edit_id='a')
  self.assertEqual(result['highlight_id'],'a')
  self.assertEqual(sent[0]['consent_version'],'server-version')
  self.assertEqual(sent[0]['confirmed_edit_id'],'a')
  self.assertEqual(sent[0]['operation'],'run')
  with self.assertRaises(ValueError):s.run_agent('edit',selected_ids=['a','b'],edit_id='b')
  s.client.agent_terms=lambda:{'version':'server-version','accepted':False}
  with self.assertRaises(AgoraError) as error:s.run_agent('find')
  self.assertEqual(error.exception.code,'AGENT_CONSENT_REQUIRED')
  self.assertEqual(len(sent),1)
 def test_agent_timeout_has_turn_id_without_resubmitting(self):
  s=self.session()
  class Client:
   def agent_terms(self):return {'version':'v3','accepted':True}
  s.client=Client();sent=[];s._send=sent.append
  with self.assertRaises(OutcomeUnknown) as error:s.run_agent('find',turn_id='test-turn',timeout=.01)
  self.assertEqual(error.exception.turn_id,'test-turn')
  self.assertEqual(len(sent),1)
 def test_stale_snapshot_cannot_roll_back_state(self):
  s=self.session();s.revision=3;s._count=2
  s._handle({'type':'canvas_snapshot','sync_protocol':2,'revision':1,'accepted_count':1,'sync_user_id':7,'items':{}})
  self.assertIn('a',s.items);self.assertEqual(s.revision,3)

class HttpTests(unittest.TestCase):
 def test_mfa_challenge_and_success_preserve_normal_authentication(self):
  client=AgoraClient();self.addCleanup(client.close);calls=[]
  def serve(request):
   calls.append(request)
   if request.url.path.endswith('/login'):return httpx.Response(200,json={'mfaRequired':True,'challengeToken':'challenge'})
   if request.url.path.endswith('/verify'):return httpx.Response(200,json={'accessToken':'user-token','user':{'nickname':'A'}})
   return httpx.Response(200,json={'nickname':'A'})
  client.http.close();client.http=httpx.Client(base_url=client.base_url,transport=httpx.MockTransport(serve))
  with self.assertRaises(MFARequired) as e:client.login('a@example.invalid','secret')
  self.assertEqual(e.exception.challenge,'challenge');self.assertIsNone(client._token)
  client.complete_mfa(e.exception.challenge,'123456');client.me();self.assertEqual(calls[-1].headers['Authorization'],'Bearer user-token')
  self.assertNotIn('user-token',repr(client))
 def test_expired_user_token_is_renewed_once_with_the_refresh_cookie(self):
  client=AgoraClient();self.addCleanup(client.close);seen=[]
  def serve(request):
   if request.url.path=='/api/auth/login':return httpx.Response(200,json={'accessToken':'old','user':{'nickname':'A'}},headers={'set-cookie':'__Secure-agora_refresh=r1; Path=/api/auth/session; Secure; HttpOnly; SameSite=Strict'})
   if request.url.path=='/api/auth/session/refresh':
    self.assertEqual(request.headers['X-Agora-Session'],'1');self.assertIn('__Secure-agora_refresh=r1',request.headers.get('cookie',''))
    return httpx.Response(200,json={'accessToken':'new'})
   seen.append(request.headers['Authorization'])
   return httpx.Response(401,json={'reason':'SESSION_EXPIRED'}) if request.headers['Authorization']=='Bearer old' else httpx.Response(200,json={'nickname':'A'})
  client.http.close();client.http=httpx.Client(base_url=client.base_url,transport=httpx.MockTransport(serve))
  client.login('a@example.invalid','secret');self.assertEqual(client.me(),{'nickname':'A'})
  self.assertEqual(seen,['Bearer old','Bearer new'])
 def test_supplied_token_without_session_cookie_is_not_refreshed(self):
  client=AgoraClient(token='external');self.addCleanup(client.close);paths=[]
  def serve(request):paths.append(request.url.path);return httpx.Response(401,json={'message':'expired'})
  client.http.close();client.http=httpx.Client(base_url=client.base_url,transport=httpx.MockTransport(serve))
  with self.assertRaises(AgoraError):client.me()
  self.assertEqual(paths,['/api/auth/me'])
 def test_client_close_closes_owned_canvas_sessions(self):
  client=AgoraClient();session=client.canvas(12);client.close();self.assertTrue(session._closed.is_set())
 def test_origin_validation_prevents_credentials_to_arbitrary_urls(self):
  for url in ['http://frelog.p-e.kr','https://user:pass@example.com','https://example.com/api']:
   with self.assertRaises(ValueError):AgoraClient(url)
  with AgoraClient(token='private') as client:
   with self.assertRaises(ValueError):client.request('GET','https://evil.test')
   self.assertNotIn('private',repr(client))
if __name__=='__main__':unittest.main()
