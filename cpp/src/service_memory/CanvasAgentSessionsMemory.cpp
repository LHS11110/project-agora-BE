#include "service_memory/CanvasServiceMemory.hpp"
#include "memory/MemoryClass.hpp"
#include "SpaceScope.hpp"

std::optional<nlohmann::json> CanvasServiceMemory::agentSessions(int id,const std::string& action,const nlohmann::json& arguments){
 const auto key="canvas:"+std::to_string(id);
 static const std::string script=R"LUA(
local root='$["agent-sessions"]'
local a=cjson.decode(ARGV[2])
local action=ARGV[1]
local now=tonumber(a.now or 0)
local function read(path)
 local raw=redis.call('JSON.GET',KEYS[1],path)
 if not raw then return nil end
 local data=cjson.decode(raw)
 return data[1]
end
local function path(kind,id) return root..'['..cjson.encode(kind)..']['..cjson.encode(id)..']' end
local function set(p,value) redis.call('JSON.SET',KEYS[1],p,cjson.encode(value)) end
local function bump() set(root..'.revision',(read(root..'.revision') or 0)+1) end
local function summary(id) return read(path('summaries',id)) end
local function ensure()
 local uid=tostring(a.user_id)
 local current=read(path('drafts',uid))
 if current then local s=summary(current);if s and s.status=='draft' then return s end end
 local id=a.new_id
 local s={id=id,owner_id=a.user_id,nickname=a.nickname or '',title='',status='draft',created_at=now,updated_at=now,message_count=0,visibility={}}
 set(path('summaries',id),s)
 redis.call('JSON.SET',KEYS[1],path('messages',id),'[]')
 redis.call('JSON.SET',KEYS[1],path('receipts',id),'{}')
 set(path('drafts',uid),id);bump()
 return s
end
if not read(root) then
 redis.call('JSON.SET',KEYS[1],root,'{"version":2,"revision":0,"summaries":{},"messages":{},"receipts":{},"drafts":{},"index":[]}')
end
-- Version 1 could stop after appending a prompt when an application ACL
-- rejected NUMINCRBY/ARRINDEX. Repair metadata only; never rerun the model.
if (read(root..'.version') or 1)<2 then
 local summaries=read(root..'.summaries') or {};local previous=read(root..'.index') or {};local indexed={}
 for _,sid in ipairs(previous) do indexed[sid]=true end
 local ordered={};local blanks={};local pointers=read(root..'.drafts') or {}
 for sid,s in pairs(summaries) do
  local sp=path('summaries',sid)
  if (s.message_count or 0)>0 then
   ordered[#ordered+1]=sid
   if s.status=='running' and not indexed[sid] then
    local receiptMap=read(path('receipts',sid)) or {};local turn=nil;local pending=nil
    for tid,r in pairs(receiptMap) do if r.status=='running' then turn=tid;pending=r;break end end
    local count=s.message_count
    local answer={id=(turn or sid)..':recovered',turn_id=turn or sid,role='assistant',kind='error',status='failed',code='AGENT_SESSION_RECOVERED',created_at=now,content='이전 요청이 세션 저장 오류로 중단되었습니다. 프롬프트는 보존되었으며 자동으로 다시 실행하지 않았습니다. 다시 사용 버튼으로 재요청할 수 있습니다.'}
    redis.call('JSON.ARRAPPEND',KEYS[1],path('messages',sid),cjson.encode(answer))
    if pending then
     set(path('messages',sid)..'['..pending.user_index..'].status','failed')
     local rp=path('receipts',sid)..'['..cjson.encode(turn)..']'
     set(rp..'.status','failed');set(rp..'.answer_index',count)
    end
    set(sp..'.message_count',count+1);set(sp..'.status','failed');set(sp..'.updated_at',now)
   end
  elseif s.status=='draft' then
   local uid=tostring(s.owner_id)
   if not blanks[uid] or pointers[uid]==sid then blanks[uid]=sid end
  end
 end
 table.sort(ordered,function(left,right)return (summaries[left].updated_at or 0)>(summaries[right].updated_at or 0) end)
 if #ordered==0 then redis.call('JSON.SET',KEYS[1],root..'.index','[]') else set(root..'.index',ordered) end
 for sid,s in pairs(summaries) do
  if s.status=='draft' and (s.message_count or 0)==0 and blanks[tostring(s.owner_id)]~=sid then
   redis.call('JSON.DEL',KEYS[1],path('summaries',sid));redis.call('JSON.DEL',KEYS[1],path('messages',sid));redis.call('JSON.DEL',KEYS[1],path('receipts',sid))
  end
 end
 redis.call('JSON.SET',KEYS[1],root..'.drafts','{}')
 for uid,sid in pairs(blanks) do set(path('drafts',uid),sid) end
 set(root..'.version',2);bump()
end
if action=='list' then
 local draft=ensure();local offset=tonumber(a.offset or 0);local limit=tonumber(a.limit or 30)
 local raw=redis.call('JSON.GET',KEYS[1],root..'.index['..offset..':'..(offset+limit)..']')
 local ids=raw and cjson.decode(raw) or {};local sessions={}
 for _,id in ipairs(ids) do local s=summary(id);if s then sessions[#sessions+1]=s end end
 local length=read(root..'.index')
 return cjson.encode({draft=draft,sessions=sessions,next_offset=(#length>offset+limit) and (offset+limit) or cjson.null,revision=read(root..'.revision')})
end
if action=='ensure' then return cjson.encode({draft=ensure(),revision=read(root..'.revision')}) end
local sid=a.session_id
local sp=path('summaries',sid)
local s=summary(sid)
if not s then return cjson.encode({error='AGENT_SESSION_NOT_FOUND'}) end
if action=='delete' then
 if s.owner_id~=a.user_id and a.admin~=true then return cjson.encode({error='AGENT_SESSION_DELETE_DENIED'}) end
 if s.status=='draft' then return cjson.encode({error='AGENT_SESSION_DRAFT_REQUIRED'}) end
 if s.status=='running' then return cjson.encode({error='AGENT_SESSION_RUNNING'}) end
 local previous=read(root..'.index') or {};local ordered={}
 for _,entry in ipairs(previous) do if entry~=sid then ordered[#ordered+1]=entry end end
 if #ordered==0 then redis.call('JSON.SET',KEYS[1],root..'.index','[]') else set(root..'.index',ordered) end
 for _,kind in ipairs({'summaries','messages','receipts'}) do redis.call('JSON.DEL',KEYS[1],path(kind,sid)) end
 bump();return cjson.encode({deleted=true,session_id=sid,revision=read(root..'.revision')})
end
if action=='meta' or action=='get' then
 if s.status=='running' and now-s.updated_at>300000 then
  set(sp..'.status','interrupted');set(sp..'.updated_at',now)
  if s.running_turn_id then
   local pending=read(path('receipts',sid)..'['..cjson.encode(s.running_turn_id)..']')
   if pending and pending.status=='running' then set(path('messages',sid)..'['..pending.user_index..'].status','interrupted') end
  end
  bump();s=summary(sid)
 end
 if action=='meta' then return cjson.encode({session=s}) end
 local count=tonumber(s.message_count or 0);local before=math.min(count,tonumber(a.before or count));local limit=tonumber(a.limit or 50)
 local messages={};local bytes=0;local first=before
 for index=before-1,math.max(0,before-limit),-1 do
  local m=read(path('messages',sid)..'['..index..']')
  if m then
   local size=#cjson.encode(m)
   if bytes+size>450000 and #messages>0 then break end
   table.insert(messages,1,m);bytes=bytes+size;first=index
  end
 end
 return cjson.encode({session=s,messages=messages,before=first,has_more=first>0,revision=read(root..'.revision')})
end
if action=='restrict' then
 local constraints=s.visibility or {};local seen={}
 for _,permission in ipairs(constraints) do seen[cjson.encode(permission)]=true end
 for _,permission in ipairs(a.visibility or {}) do
  local encoded=cjson.encode(permission)
  if not seen[encoded] then constraints[#constraints+1]=permission;seen[encoded]=true end
 end
 set(sp..'.visibility',constraints);bump()
 return cjson.encode({revision=read(root..'.revision')})
end
local receiptPath=path('receipts',sid)..'['..cjson.encode(a.turn_id)..']'
if action=='begin' then
 if s.owner_id~=a.user_id then return cjson.encode({error='AGENT_SESSION_READ_ONLY'}) end
 local old=read(receiptPath)
 if old then
  if old.digest~=a.digest then return cjson.encode({error='AGENT_SESSION_TURN_CONFLICT'}) end
  if old.status=='running' then return cjson.encode({error='AGENT_SESSION_RUNNING',duplicate=true}) end
  local answer=read(path('messages',sid)..'['..old.answer_index..']')
  return cjson.encode({duplicate=true,replay=answer,session=s,draft=ensure(),revision=read(root..'.revision')})
 end
 if s.status=='running' and now-s.updated_at<=300000 then return cjson.encode({error='AGENT_SESSION_RUNNING'}) end
 local count=tonumber(s.message_count or 0)
 redis.call('JSON.ARRAPPEND',KEYS[1],path('messages',sid),ARGV[3])
 set(receiptPath,{digest=a.digest,status='running',user_index=count})
 set(sp..'.message_count',count+1);set(sp..'.status','running');set(sp..'.running_turn_id',a.turn_id);set(sp..'.updated_at',now)
 local constraints=s.visibility or {}
 for _,permission in ipairs(a.visibility or {}) do constraints[#constraints+1]=permission end
 set(sp..'.visibility',constraints)
 if count==0 then
  set(sp..'.title',a.title)
  if a.terms_version then set(sp..'.terms_version',a.terms_version) end
 end
 local previousIndex=read(root..'.index') or {};local ordered={sid}
 for _,entry in ipairs(previousIndex) do if entry~=sid then ordered[#ordered+1]=entry end end
 set(root..'.index',ordered)
 local pointer=read(path('drafts',tostring(a.user_id)))
 if pointer==sid then redis.call('JSON.DEL',KEYS[1],path('drafts',tostring(a.user_id))) end
 local draft=ensure();bump()
 return cjson.encode({session=summary(sid),draft=draft,revision=read(root..'.revision')})
end
if action=='finish' then
 local receipt=read(receiptPath)
 if not receipt then return cjson.encode({error='AGENT_SESSION_TURN_NOT_FOUND'}) end
 if receipt.status~='running' then return cjson.encode({duplicate=true,revision=read(root..'.revision')}) end
 local count=tonumber(s.message_count or 0)
 redis.call('JSON.ARRAPPEND',KEYS[1],path('messages',sid),ARGV[3])
 local status=a.status
 set(path('messages',sid)..'['..receipt.user_index..'].status',status)
 set(sp..'.message_count',count+1);if s.running_turn_id==a.turn_id then set(sp..'.status',status);set(sp..'.updated_at',now) end
 set(receiptPath..'.status',status);set(receiptPath..'.answer_index',count);bump()
 return cjson.encode({session=summary(sid),revision=read(root..'.revision')})
end
return cjson.encode({error='AGENT_SESSION_ACTION_INVALID'})
)LUA";
 auto args=arguments;const auto message=args.value("message",nlohmann::json::object());args.erase("message");
 const auto raw=memory_->executeAtomic(key,{"EVAL",script,"1",key,action,args.dump(),message.dump()},{"$[\"agent-sessions\"]"});
 if(!raw)return std::nullopt;
 try{return nlohmann::json::parse(*raw);}catch(...){return std::nullopt;}
}
