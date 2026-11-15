from pathlib import Path
import concurrent.futures,hashlib,json,os,subprocess,uuid,time
root=Path(__file__).resolve().parents[1]
db=Path(os.environ.get('AGORA_DB_DIR',str(root.parent.parent/'project-agora-DB')))
source=(root/'src/service_memory/CanvasAgentSessionsMemory.cpp').read_text()
script=source.split('R"LUA(',1)[1].split(')LUA"',1)[0]
cli=None
for container in ('agora-redis-primary','agora-redis-replica-1','agora-redis-replica-2'):
    candidate=[str(db/'redis/redis-container-cli.sh'),container,'app','--raw']
    role=subprocess.run(candidate+['ROLE'],text=True,capture_output=True)
    if role.returncode==0 and role.stdout.startswith('master'):
        cli=candidate;break
if cli is None:raise RuntimeError('No primary reachable with application Redis credentials')
key='canvas:agent-session-test-'+str(uuid.uuid4())
def raw(*args):return subprocess.check_output(cli+list(args),text=True)
def call(action,user=1,**args):
    payload={'user_id':user,'nickname':f'user-{user}','now':int(time.time()*1000),'new_id':'session-'+str(uuid.uuid4()),**args}
    message=payload.pop('message',{})
    return json.loads(raw('EVAL',script,'1',key,action,json.dumps(payload),json.dumps(message,ensure_ascii=False)))
try:
    raw('JSON.SET',key,'$',json.dumps({'items':{}}))
    first=call('list');sid=first['draft']['id']
    with concurrent.futures.ThreadPoolExecutor(max_workers=6) as pool:
        ids=list(pool.map(lambda _:call('ensure')['draft']['id'],range(12)))
    assert set(ids)=={sid},ids
    other=call('list',user=2)['draft']['id'];assert other!=sid
    text='기존 프롬프트를 보존해주세요'
    begun=call('begin',session_id=sid,turn_id='turn-one',digest='digest-one',title=text,visibility=[],message={'id':'turn-one','turn_id':'turn-one','role':'user','content':text,'status':'running'})
    newdraft=begun['draft']['id'];assert newdraft!=sid
    assert call('ensure')['draft']['id']==newdraft
    assert call('begin',session_id=sid,turn_id='turn-one',digest='digest-one',title=text,message={})['error']=='AGENT_SESSION_RUNNING'
    assert call('begin',user=2,session_id=sid,turn_id='attack',digest='attack',title='attack',message={})['error']=='AGENT_SESSION_READ_ONLY'
    call('finish',session_id=sid,turn_id='turn-one',status='failed',message={'id':'turn-one:result','role':'assistant','kind':'error','code':'AGENT_PROVIDER_BUSY','content':'요청이 너무 많습니다. 잠시 후 다시 시도해주세요.','status':'failed'})
    page=call('get',session_id=sid,limit=1);assert page['has_more'] and page['before']==1
    original=call('get',session_id=sid,before=1,limit=1)['messages'][0]
    assert original['content']==text and original['status']=='failed'
    replay=call('begin',session_id=sid,turn_id='turn-one',digest='digest-one',title=text,message={})
    assert replay['duplicate'] and replay['replay']['code']=='AGENT_PROVIDER_BUSY'
    assert call('get',session_id=sid)['session']['message_count']==2
    assert call('begin',session_id=sid,turn_id='turn-one',digest='different',title=text,message={})['error']=='AGENT_SESSION_TURN_CONFLICT'
    for i in range(3):
        turn='turn-'+str(i+2)
        call('begin',session_id=sid,turn_id=turn,digest=turn,title=text,visibility=[],message={'id':turn,'role':'user','content':str(i),'status':'running'})
        call('finish',session_id=sid,turn_id=turn,status='complete',message={'id':turn+':result','role':'assistant','content':'done','status':'complete'})
    listed=call('list',user=2);assert listed['sessions'][0]['id']==sid and listed['draft']['id']==other
    call('restrict',session_id=sid,visibility=[['writers'],['research']])
    assert call('meta',session_id=sid)['session']['visibility']==[['writers'],['research']]
    stored=json.loads(raw('JSON.GET',key,'$["agent-sessions"]'))[0]
    assert len(stored['drafts'])==2
    assert all(stored['summaries'][draft]['status']=='draft' for draft in stored['drafts'].values())
    assert len(stored['index'])==1 and len(stored['messages'][sid])==8
    stale=call('ensure',user=3)['draft']['id']
    old=int(time.time()*1000)-400000
    call('begin',user=3,session_id=stale,turn_id='old-turn',digest='old',title='stale',now=old,message={'id':'old-turn','role':'user','content':'old','status':'running'})
    assert call('meta',user=3,session_id=stale)['session']['status']=='interrupted'
    assert call('get',user=3,session_id=stale)['messages'][0]['status']=='interrupted'
    call('begin',user=3,session_id=stale,turn_id='new-turn',digest='new',title='new',message={'id':'new-turn','role':'user','content':'new','status':'running'})
    call('finish',user=3,session_id=stale,turn_id='old-turn',status='complete',message={'id':'old-answer','role':'assistant','content':'late','status':'complete'})
    assert call('meta',user=3,session_id=stale)['session']['status']=='running'
    call('finish',user=3,session_id=stale,turn_id='new-turn',status='complete',message={'id':'new-answer','role':'assistant','content':'new','status':'complete'})
    assert call('meta',user=3,session_id=stale)['session']['status']=='complete'
    assert call('delete',user=2,session_id=sid)['error']=='AGENT_SESSION_DELETE_DENIED'
    assert call('delete',session_id=newdraft)['error']=='AGENT_SESSION_DRAFT_REQUIRED'
    running=call('ensure',user=4)['draft']['id']
    call('begin',user=4,session_id=running,turn_id='running-delete',digest='run',title='running',message={'id':'running-delete','role':'user','content':'run','status':'running'})
    assert call('delete',user=4,session_id=running)['error']=='AGENT_SESSION_RUNNING'
    assert call('delete',user=2,session_id=sid,admin=True)['deleted']
    assert call('get',session_id=sid)['error']=='AGENT_SESSION_NOT_FOUND'
    deleted=json.loads(raw('JSON.GET',key,'$["agent-sessions"]'))[0]
    assert all(sid not in deleted[kind] for kind in ('summaries','messages','receipts','index'))
    assert call('ensure')['draft']['id']==newdraft
    assert call('delete',user=3,session_id=stale)['deleted']
    legacy={'version':1,'revision':0,'drafts':{'1':'valid-blank'},'index':[],
      'summaries':{'old':{'id':'old','owner_id':1,'nickname':'fixture','title':'retained','status':'running','updated_at':1,'message_count':1,'visibility':[]},'valid-blank':{'id':'valid-blank','owner_id':1,'nickname':'fixture','status':'draft','message_count':0},'orphan-blank':{'id':'orphan-blank','owner_id':1,'nickname':'fixture','status':'draft','message_count':0}},
      'messages':{'old':[{'id':'old-turn','role':'user','content':'recover existing prompt','status':'running'}],'valid-blank':[],'orphan-blank':[]},
      'receipts':{'old':{'old-turn':{'digest':'old-digest','status':'running','user_index':0}},'valid-blank':{},'orphan-blank':{}}}
    raw('JSON.SET',key,'$["agent-sessions"]',json.dumps(legacy))
    repaired=call('list');assert repaired['draft']['id']=='valid-blank'
    assert repaired['sessions'][0]['id']=='old' and repaired['sessions'][0]['status']=='failed'
    repairedMessages=call('get',session_id='old')['messages']
    assert repairedMessages[0]['content']=='recover existing prompt'
    assert repairedMessages[1]['code']=='AGENT_SESSION_RECOVERED'
    assert len(call('get',session_id='old')['messages'])==2
    repairedRoot=json.loads(raw('JSON.GET',key,'$["agent-sessions"]'))[0]
    assert repairedRoot['version']==2 and 'orphan-blank' not in repairedRoot['summaries']
    print('PASS: concurrent unique per-user drafts, automatic next draft, shared sessions, failed prompt retention, idempotent turns, ownership, message pagination, legacy ACL-error recovery and atomic history')
finally:raw('DEL',key)
