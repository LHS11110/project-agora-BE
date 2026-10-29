from pathlib import Path
import subprocess,json,uuid,os
root=Path(__file__).resolve().parents[1]
db=Path(os.environ.get('AGORA_DB_DIR',str(root.parent.parent/'project-agora-DB')))
s=(root/'src/service_memory/CanvasServiceMemory.cpp').read_text().split('CanvasServiceMemory::applyBrokerBatch',1)[1].split('R"LUA(',1)[1].split(')LUA"')[0]
cli=[str(db/'redis/redis-ha-cli.sh'),'--raw']
key='canvas:sync-test-'+str(uuid.uuid4())
def call(*args):return subprocess.check_output(cli+list(args),text=True)
try:
 call('JSON.SET',key,'$',json.dumps({'items':{}}))
 item={'kind':'text','permission':'default','text':'array preservation','x':0.1234567890123456,'automerge_snapshot':'fixture','automerge_changes':[],'members':[]}
 encoded={'sync-json-preservation':json.dumps(item,separators=(',',':'))}
 args=['EVAL',s,'1',key,'$["__broker_sync"]["clients"]["json-preservation"]','1','json-preservation','digest',json.dumps(encoded),json.dumps({'sync-json-preservation':-1})]
 r=json.loads(call(*args));assert r['accepted_count']==1,r
 actual=json.loads(call('JSON.GET',key,'$["items"]["sync-json-preservation"]'))[0]
 assert actual['automerge_changes']==[] and actual['members']==[] and actual['x']==item['x']
 r=json.loads(call(*args));assert r['duplicate'] and r['revision']==1
 mismatch=args.copy();mismatch[7]='mismatched-digest';r=json.loads(call(*mismatch));assert r['error']=='CLIENT_REQUEST_MISMATCH',r
 conflict=args.copy();conflict[5]='2';conflict[6]='request-two';conflict[7]='digest-two';r=json.loads(call(*conflict));assert r['error']=='CANVAS_WRITE_CONFLICT',r
 assert json.loads(call('JSON.GET',key,'$["__broker_sync"]["revision"]'))==[1]
 print('Redis Lua: empty arrays and precision preserved, duplicate applied once, mismatched retries and stale writes refused atomically.')
finally:call('DEL',key)
