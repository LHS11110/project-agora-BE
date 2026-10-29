"""Run production Lua against a temporary key; never touches application canvases."""
from pathlib import Path
import json, os, subprocess, uuid
root = Path(__file__).resolve().parents[1]
db = Path(os.environ.get('AGORA_DB_DIR', str(root.parent.parent / 'project-agora-DB')))
source = (root / 'src/service_memory/CanvasServiceMemory.cpp').read_text()
def script(function):
    return source.split('CanvasServiceMemory::' + function, 1)[1].split('R"LUA(', 1)[1].split(')LUA"', 1)[0]
cli = [str(db / 'redis/redis-ha-cli.sh'), '--raw']
key = 'canvas:mentions-test-' + str(uuid.uuid4())
def call(*args):
    return subprocess.check_output(cli + list(args), text=True).strip()
room_path = '$["items"]["general"]'
receipt = room_path + '["requests"]["1:request-one"]'
message = dict(type='chat', room_id='general', text='@test user#1 hello', sender='owner', tag_number=1,
               sender_user_id=1, sequence=1, created_at=123, request_id='request-one', mentions={'2':True, '3':True})
room = dict(type='chat_room', permission='default', data=[], requests={}, next_sequence=1)
def commit(msg):
    return call('EVAL', script('commitChatMessage'), '1', key, room_path, receipt, json.dumps(room), json.dumps(msg))
try:
    call('JSON.SET', key, '$', json.dumps({'items':{'unrelated':{'members':[],'x':0.1234567890123456}}}))
    assert json.loads(commit(message))['sequence'] == 1
    mid='general:1'
    records=json.loads(call('JSON.GET',key,'$["mention-states"]'))[0]
    assert records['2'][mid]['unread'] and records['3'][mid]['unread']
    user_path='$["mention-states"]["2"]'
    path=user_path+'["general:1"]'
    assert call('EVAL',script('storeMentionState'),'1',key,user_path,path,'{}','read','2') == 'OK'
    assert json.loads(call('JSON.GET',key,path))[0]['unread'] is False
    stored=json.loads(call('JSON.GET',key,room_path+'["data"]'))[0]
    assert stored[0]['mentions']=={'2':False,'3':True}
    commit(message)
    assert json.loads(call('JSON.GET',key,room_path+'["data"]'))[0].__len__() == 1
    assert json.loads(call('JSON.GET',key,path))[0]['unread'] is False
    assert commit(dict(message,text='changed')) == 'CLIENT_REQUEST_MISMATCH'
    unrelated=json.loads(call('JSON.GET',key,'$.items.unrelated'))[0]
    assert unrelated['members']==[] and unrelated['x']==0.1234567890123456
    print('PASS: atomic mention creation, per-user reads, retry deduplication, read-state preservation, array and precision preservation')
finally:
    call('DEL',key)
