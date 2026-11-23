#include "service/AgentClient.hpp"
#include "service/CanvasAgentTools.hpp"
#include "service/CanvasAgentSessions.hpp"
#include "service/CanvasActivity.hpp"
#include "service/CanvasQueryService.hpp"
#include "service/CanvasQueryRules.hpp"
#include "service/CanvasSettingsService.hpp"
#include "service/CanvasPublicEnrollment.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "service_memory/CanvasServiceMemory.hpp"
#include "service_memory/CanvasEventMemory.hpp"
#include "service_memory/CanvasItemRules.hpp"
#include "service_memory/RegistryServiceMemory.hpp"
#include <chrono>
#include <cstdlib>
#include <string_view>
#include <algorithm>
#include <iomanip>
#include <sstream>
#include <openssl/sha.h>
#include "service/CanvasBrokerOperations.hpp"
#include "service/CanvasMentions.hpp"
#include "service/RuntimeKernelClient.hpp"
#include "service/CanvasWorkspaceFiles.hpp"

namespace {
using Json = nlohmann::json;

bool hasAdultAgentAccess(long long user_id) {
    const char* configured = std::getenv("AGENT_VERIFIED_ADULT_USER_IDS");
    if (!configured || user_id <= 0) return false;
    const auto user = std::to_string(user_id);
    std::string_view entries(configured);
    while (!entries.empty()) {
        const auto separator = entries.find(',');
        if (entries.substr(0, separator) == user) return true;
        if (separator == std::string_view::npos) break;
        entries.remove_prefix(separator + 1);
    }
    return false;
}

Json routing(const Json& input) {
    const auto route = input.value("route", Json::object());
    if (!route.is_object() || !route.contains("canvas_id") || !route["canvas_id"].is_number_integer()
        || route["canvas_id"].get<int>() <= 0 || !route.contains("connection_id") || !route["connection_id"].is_string()
        || route["connection_id"].get<std::string>().empty() || route["connection_id"].get<std::string>().size() > 128
        || !route.contains("request_id") || !route["request_id"].is_string() || route["request_id"].get<std::string>().empty() || route["request_id"].get<std::string>().size() > 128)
        throw std::invalid_argument("ROUTE_INVALID");
    if (route.contains("scope")) {
        if (!route["scope"].is_string()) throw std::invalid_argument("ROUTE_INVALID");
        try {
            (void)parseSpaceScope(route["scope"].get<std::string>());
        } catch (const std::invalid_argument&) {
            throw std::invalid_argument("ROUTE_INVALID");
        }
        return {{"canvas_id", route["canvas_id"]}, {"connection_id", route["connection_id"]},
                {"request_id", route["request_id"]}, {"scope", route["scope"]}};
    }
    return {{"canvas_id", route["canvas_id"]}, {"connection_id", route["connection_id"]}, {"request_id", route["request_id"]}};
}
SpaceScope routeScope(const Json& route) {
    return route.contains("scope") && route["scope"].is_string()
        ? parseSpaceScope(route["scope"].get<std::string>())
        : SpaceScope::Canvas;
}
Json envelope(const Json& route, Json response, Json broadcasts = Json::array()) {
    response["request_id"] = route.is_object() ? route.value("request_id", std::string()) : std::string();
    return {{"route", route}, {"response", std::move(response)}, {"broadcasts", std::move(broadcasts)}};
}
std::optional<Json> readItem(CanvasServiceMemory& memory, SpaceScope scope, int id, const std::string& item) {
    const auto raw = memory.readItem(scope, id, item);
    if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
    const auto matches = Json::parse(*raw);
    if (!matches.is_array()) throw std::runtime_error("STORAGE_UNAVAILABLE");
    return matches.empty() ? std::nullopt : std::optional<Json>(matches.front());
}
std::string commentRoomId(const std::string& object_id) {
    unsigned char digest[SHA256_DIGEST_LENGTH];
    SHA256(reinterpret_cast<const unsigned char*>(object_id.data()), object_id.size(), digest);
    std::ostringstream result;
    result << "object-comment-" << std::hex << std::setfill('0');
    for (auto byte : digest) result << std::setw(2) << static_cast<unsigned>(byte);
    return result.str();
}
}
Json CanvasQueryService::execute(const Json& input) {
    return execute(input, {});
}
Json CanvasQueryService::execute(const Json& input, const OutputHandler& on_output) {
    Json route = Json::object();
    try {
        route = routing(input);
        if (input.value("command", "") == "connect") return join(input);
        if (input.value("command", "") == "query") return query(input, on_output);
        throw std::invalid_argument("QUERY_COMMAND_INVALID");
    } catch (const std::invalid_argument& error) {
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", error.what()}});
    } catch (const Json::exception&) {
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", "QUERY_DATA_INVALID"}});
    } catch (const std::runtime_error& error) {
        const std::string code=error.what();
        const bool runtime_error=code.rfind("RUNTIME_",0)==0||code.rfind("AGENT_",0)==0;
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", runtime_error||code=="STORAGE_UNAVAILABLE"||code=="QUERY_OUTCOME_UNKNOWN"||code=="CANVAS_NOT_READY"?code:"QUERY_UNAVAILABLE"}});
    } catch (...) {
        return envelope(route, {{"type", "error"}, {"ok", false}, {"code", "QUERY_UNAVAILABLE"}});
    }
}
Json CanvasQueryService::join(const Json& input) {
    auto route = routing(input); int id = route["canvas_id"];
    const SpaceScope scope = routeScope(route);
    if (input.value("ws_port", 0) != logical_ws_port_) throw std::invalid_argument("SERVER_ROUTE_INVALID");
    const auto principal = input.value("principal", Json::object());
    if (!principal.is_object() || principal.value("canvas_id", 0) != id
        || (principal.contains("scope") && (!principal["scope"].is_string() || principal["scope"].get<std::string>() != spaceScopeWire(scope))))
        throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    const auto nickname = principal.value("nickname", "");
    const auto tag = principal.value("tag_number", -1);
    const auto revision = principal.value("settings_revision", -1LL);
    if (nickname.empty() || tag < 0 || revision < 0) throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    RegistryServiceMemory registry(pool_.getDbHost(), pool_.getDbPort());
    const auto user_id = registry.getActiveUserId(nickname, tag);
    if (user_id <= 0 || !pool_.isCanvasAccessAuthorized(scope, id, user_id, revision))
        throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
    auto canvas = pool_.loadForQuery(scope, id);
    if (!canvas) throw std::runtime_error("CANVAS_NOT_READY");
    std::lock_guard<std::mutex> lock(canvas->settings_mutex);
    if (canvas->unloading) throw std::runtime_error("CANVAS_NOT_READY");
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    auto raw = memory.loadCanvas(scope, id); if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
    auto doc = Json::parse(*raw);
    if (doc.value("settings-revision", 0LL) != revision) throw std::invalid_argument("SETTINGS_CHANGED");
    auto [groups, admin] = groupsForUser(doc, user_id);
    if(groups.empty()&&!admin)throw std::invalid_argument("CANVAS_READ_DENIED");
    auto write_groups=groupsForUser(doc,user_id,"write").first;
    auto execute_groups=groupsForUser(doc,user_id,"execute").first;
    auto member_groups=canvasMemberGroups(doc,user_id);
    const auto group_bits=canvasGroupBitLayout(doc);
    CanvasAclScope access(group_bits,{&groups,&write_groups,&execute_groups,&member_groups});
    Json group_masks{{"member",canvasMaskHex(access.index.mask(member_groups))},{"read",canvasMaskHex(access.index.mask(groups))},{"write",canvasMaskHex(access.index.mask(write_groups))},{"execute",canvasMaskHex(access.index.mask(execute_groups))}};
    Json visible = Json::object();
    if (input.value("include_items",true) && doc.contains("items") && doc["items"].is_object()) for (const auto& entry : doc["items"].items())
        if (entry.value().value("kind", "") != "host-tombstone" && queryCanAccess(entry.value(), groups, admin)) {
            auto item = entry.value(); if (agora::canvas::isChatRoomItem(item)) { item.erase("data"); item.erase("requests"); } visible[entry.key()] = std::move(item);
        }
    const auto client = input.value("client_id", "");
    const auto sync = memory.brokerSyncState(scope, id, std::to_string(user_id) + ":" + client);
    if (!sync) throw std::runtime_error("STORAGE_UNAVAILABLE");
    const Json previousPeople = doc.value("people", Json::array()), previousGroups = doc.value("inner-group", Json::object());
    const bool participantJoined = enrollPublicParticipant(doc, user_id);
    if (participantJoined) {
        const std::vector<std::pair<std::string, Json>> changes{{"people",doc["people"]},{"inner-group",doc["inner-group"]}};
        if (memory.compareAndSetFields(scope, id, revision, changes) != CanvasServiceMemory::CompareSetResult::Applied) throw std::runtime_error("STORAGE_UNAVAILABLE");
        CanvasSnapshotMemory es(pool_.getEsHost(), pool_.getEsPort(), "", "", "", scope);
        if (!es.patchCanvasFields(id, {{"people",doc["people"]},{"inner-group",doc["inner-group"]}})) {
            memory.compareAndSetFields(scope, id, revision, {{"people",previousPeople},{"inner-group",previousGroups}});
            es.patchCanvasFields(id, {{"people",previousPeople},{"inner-group",previousGroups}});
            throw std::runtime_error("STORAGE_UNAVAILABLE");
        }
    }
    const auto receipt = sync->value("receipt", Json());
    Json identity{{"user_id", user_id}, {"nickname", nickname}, {"tag_number", tag},
        {"group_bits",group_bits},{"group_masks",group_masks},{"groups", groups}, {"member_groups",member_groups}, {"write_groups",write_groups}, {"execute_groups",execute_groups}, {"is_admin", admin}, {"settings_revision", revision}, {"canvas_id", id}, {"scope", std::string(spaceScopeWire(scope))}};
    return {{"route", route}, {"identity", identity},
        {"response", {{"type", "init_items"}, {"canvas_id", id}, {"scope", std::string(spaceScopeWire(scope))}, {"items", visible}, {"group_bits",group_bits},{"group_masks",group_masks},{"groups", groups}, {"member_groups",member_groups}, {"write_groups",write_groups}, {"execute_groups",execute_groups}, {"is_admin",admin},
                      {"agent_adult_access", scope == SpaceScope::Canvas && hasAdultAgentAccess(user_id)},
                      {"participant_joined", participantJoined}, {"edit_protocol",2},{"points_protocol",1},{"sync_protocol", 2}, {"sync_user_id",user_id}, {"revision", (*sync)["revision"]}, {"client_id", client}, {"accepted_count", receipt.is_null()?0LL:receipt.value("count",0LL)}, {"canvas_name", doc.value("canvas-name", "")}, {"status", "connected"}, {"server_protocol", "Phoenix"}}}};
}
Json CanvasQueryService::query(const Json& input, const OutputHandler& on_output) {
    auto route = routing(input); int id = route["canvas_id"];
    const SpaceScope scope = routeScope(route);
    // Only the authenticated broker socket invokes this handler. Identity is
    // established by broker JWT authentication and authoritative canvas lookup.
    const auto identity = input.value("identity", Json::object());
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count();
    if (!identity.is_object() || identity.value("canvas_id", 0) != id
        || identity.value("scope", std::string("canvas")) != spaceScopeWire(scope)
        || identity.value("connection_id", "") != route["connection_id"].get<std::string>()
        || identity.value("expires_at", 0LL) <= now || identity.value("user_id", 0) <= 0
        || !identity.contains("groups") || !identity["groups"].is_array()
        || !identity.contains("is_admin") || !identity["is_admin"].is_boolean())
        throw std::invalid_argument("QUERY_UNAUTHORIZED");
    auto canvas = pool_.getCanvas(scope, id);
    if (!canvas) throw std::invalid_argument("CANVAS_NOT_READY");
    std::unique_lock<std::mutex> lock(canvas->settings_mutex);
    if (canvas->unloading) throw std::invalid_argument("CANVAS_NOT_READY");
    canvas->touchQuery();
    auto event = input.value("event", Json::object());
    if (!event.is_object()) throw std::invalid_argument("QUERY_INVALID");
    const auto type = event.value("type", "");
    if (type == "canvas_access_refresh") {
        CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
        const auto loaded = memory.loadCanvasSettings(scope, id);
        if (!loaded || !loaded->is_object()) throw std::runtime_error("STORAGE_UNAVAILABLE");
        const auto& doc = *loaded;
        const int user = identity["user_id"];
        if (!canvasIsPublic(doc) && !canvasHasMember(doc, user)) throw std::invalid_argument("CANVAS_ACCESS_UNAUTHORIZED");
        auto [read, admin] = groupsForUser(doc, user);
        if (read.empty() && !admin) throw std::invalid_argument("CANVAS_READ_DENIED");
        const auto write = groupsForUser(doc, user, "write").first;
        const auto execute = groupsForUser(doc, user, "execute").first;
        const auto members = canvasMemberGroups(doc, user);
        const auto bits = canvasGroupBitLayout(doc);
        CanvasAclScope acl_scope(bits, {&read, &write, &execute, &members});
        auto updated = identity;
        updated["scope"] = std::string(spaceScopeWire(scope));
        updated["groups"] = read; updated["write_groups"] = write; updated["execute_groups"] = execute;
        updated["member_groups"] = members; updated["is_admin"] = admin; updated["group_bits"] = bits;
        updated["settings_revision"] = doc.value("settings-revision", 0LL);
        updated["group_masks"] = {{"read",canvasMaskHex(acl_scope.index.mask(read))},{"write",canvasMaskHex(acl_scope.index.mask(write))},{"execute",canvasMaskHex(acl_scope.index.mask(execute))},{"member",canvasMaskHex(acl_scope.index.mask(members))}};
        auto settings = CanvasSettingsService::execute(pool_, canvas, id, user, identity["nickname"], identity["tag_number"], Json{{"type","canvas_settings_get"}}, route["request_id"]);
        auto result = envelope(route, settings.response);
        result["identity"] = updated;
        return result;
    }
    if (canvas->getSettingsRevision() != identity.value("settings_revision", -1LL)) throw std::invalid_argument("SETTINGS_CHANGED");
    auto groups = identity["groups"].get<std::unordered_set<std::string>>(); const bool admin = identity.value("is_admin", false);
    const auto write_groups=identity.value("write_groups",Json::array()).get<std::unordered_set<std::string>>();
    const auto execute_groups=identity.value("execute_groups",Json::array()).get<std::unordered_set<std::string>>();
    const auto member_groups=identity.value("member_groups",identity["groups"]).get<std::unordered_set<std::string>>();
    CanvasAclScope access(identity.value("group_bits",Json::object()),{&groups,&write_groups,&execute_groups,&member_groups});
    CanvasServiceMemory memory(canvas->getRedisIp(), canvas->getRedisPort());
    // The broker admits a peer into a call channel only if the channel item is
    // a voice channel this identity can read.
    if (type == "voice_channel_access") {
        const auto channel = event.value("channel", "");
        if (!validQueryItemId(channel)) throw std::invalid_argument("VOICE_CHANNEL_DENIED");
        const auto item = readItem(memory, scope, id, channel);
        if (!item || item->value("kind", "") != "channel" || item->value("channelType", "") != "voice"
            || !queryCanAccess(*item, groups, admin)) throw std::invalid_argument("VOICE_CHANNEL_DENIED");
        return envelope(route, {{"type", "voice_channel_access"}, {"ok", true}, {"channel", channel}});
    }
    if (type == "workspace_files") {
        agora::workspace::validate(event);
        const auto operation = event["operation"].get<std::string>();
        if (groups.empty() && !admin) throw std::invalid_argument("CANVAS_READ_DENIED");
        if (!agora::workspace::readOnly(operation) && write_groups.empty() && !admin)
            throw std::invalid_argument("ITEM_WRITE_DENIED");
        Json request = event;
        request.erase("type");
        request.erase("request_id");
        // Receipt identity is server-owned and survives token rotation/reconnection.
        request["requestId"] = std::to_string(identity["user_id"].get<int>()) + ":" + event["request_id"].get<std::string>();
        const auto result = RuntimeKernelClient(runtime_socket_).request({{"canvasId", runtimeTargetId(scope, id)}, {"action", "workspace"}, {"request", request}});
        if (result.contains("error")) return envelope(route, {{"type", "workspace_files_result"}, {"ok", false}, {"code", result["error"]}});
        Json broadcasts = Json::array();
        if (!agora::workspace::readOnly(operation)) {
            Json change{{"type", "workspace_files_changed"}, {"path", event["path"]}, {"operation", operation}, {"request_id", event["request_id"]}};
            change["created"] = operation == "mkdir" || result.value("created", false);
            if (event.contains("target")) change["target"] = event["target"];
            // Only invalidations are broadcast. Every content read rechecks current access.
            broadcasts.push_back({{"all", true}, {"event", change}});
        }
        return envelope(route, {{"type", "workspace_files_result"}, {"ok", true}, {"result", result}}, broadcasts);
    }
    if(type=="agent_tool"){
        if(scope!=SpaceScope::Canvas)throw std::invalid_argument("AGENT_SCOPE_DENIED");
        if(!hasAdultAgentAccess(identity["user_id"].get<long long>()))throw std::invalid_argument("AGENT_ADULT_ACCESS_REQUIRED");
        const auto name=event.value("name",std::string());const auto args=event.value("arguments",Json::object());
        if(!args.is_object()||args.dump().size()>800000)throw std::invalid_argument("AGENT_ARGUMENTS_INVALID");
        auto readable=[&](const std::string& key){if(!validQueryItemId(key))throw std::invalid_argument("ITEM_ID_INVALID");const auto target=readItem(memory,scope,id,key);if(!target||!target->contains("kind")||target->value("kind","")=="host-tombstone"||!queryCanAccess(*target,groups,admin))throw std::invalid_argument("ITEM_ACCESS_DENIED");return *target;};
        if(name=="get_objects"){
            const auto ids=args.value("ids",Json::array());if(!ids.is_array()||ids.empty()||ids.size()>6)throw std::invalid_argument("AGENT_ARGUMENTS_INVALID");Json objects=Json::array(),visibility=Json::array();
            for(const auto& key:ids){const auto item=readable(key.get<std::string>());visibility.push_back(agora::agent::sessionPermission(item));const auto source=agora::agent::publicItem(item,true);if(source.dump().size()>100000)throw std::invalid_argument("AGENT_OBJECT_TOO_LARGE");objects.push_back({{"id",key},{"revision",item.value("__broker_revision",0LL)},{"item",source}});}
            auto reply=envelope(route,{{"type","agent_tool_result"},{"result",{{"objects",objects}}}});reply["agent_visibility"]=visibility;return reply;
        }
        if(name=="list_objects"){
            const auto offset=args.value("offset",0),limit=args.value("limit",20);if(offset<0||offset>10000000||limit<1||limit>50)throw std::invalid_argument("AGENT_ARGUMENTS_INVALID");
            const auto page=memory.agentItemIds(id,offset,100);if(!page)throw std::runtime_error("STORAGE_UNAVAILABLE");Json objects=Json::array(),visibility=Json::array();int examined=0;
            for(const auto& key:page->value("ids",Json::array())){
                ++examined;const auto item=readItem(memory,scope,id,key.get<std::string>());if(!item||!item->contains("kind")||item->value("kind","")=="host-tombstone"||!queryCanAccess(*item,groups,admin))continue;
                const auto kind=args.value("kind",std::string()),search=args.value("query",std::string());if(!kind.empty()&&item->value("kind","")!=kind)continue;
                const auto title=item->value("title",std::string())+item->value("filename",std::string());if(!search.empty()&&title.find(search)==std::string::npos)continue;
                visibility.push_back(agora::agent::sessionPermission(*item));objects.push_back({{"id",key},{"item",agora::agent::publicItem(*item,false)}});if(objects.size()>=static_cast<std::size_t>(limit))break;
            }
            const bool more=examined<100?objects.size()>=static_cast<std::size_t>(limit):page->value("more",false);
            auto reply=envelope(route,{{"type","agent_tool_result"},{"result",{{"objects",objects},{"next_offset",more?Json(offset+examined):Json(nullptr)}}}});reply["agent_visibility"]=visibility;return reply;
        }
        if(write_groups.empty()&&!admin)throw std::invalid_argument("ITEM_WRITE_DENIED");
        Json operations=Json::array(),ids=Json::array(),visibility=Json::array();
        if(name=="create_objects"){
            const auto settings=memory.loadCanvasSettings(scope,id);
            if(!settings||!settings->is_object())throw std::runtime_error("STORAGE_UNAVAILABLE");
            if(canvasIsPublic(*settings))throw std::invalid_argument("AGENT_PUBLIC_PUBLISH_DENIED");
            const auto values=args.value("items",Json::array());if(!values.is_array()||values.empty()||values.size()>10)throw std::invalid_argument("AGENT_ARGUMENTS_INVALID");
            for(auto item:values){agora::agent::fields(item);agora::agent::snapGeometry(item);const auto key=agora::agent::randomId();item["permission"]=member_groups;item["aiGenerated"]=true;visibility.push_back(agora::agent::sessionPermission(item));operations.push_back({{"action","create"},{"id",key},{"patch",item}});ids.push_back(key);}
        }else if(name=="patch_objects"||name=="delete_objects"){
            if(name=="delete_objects"&&!event.value("allow_delete",false))throw std::invalid_argument("AGENT_DELETE_DISABLED");
            const auto values=args.value(name=="patch_objects"?"changes":"objects",Json::array());if(!values.is_array()||values.empty()||values.size()>10)throw std::invalid_argument("AGENT_ARGUMENTS_INVALID");
            for(const auto& value:values){const auto key=value.at("id").get<std::string>();const auto item=readable(key);visibility.push_back(agora::agent::sessionPermission(item));if(!queryCanAccess(item,write_groups,admin)||queryObjectLocked(item))throw std::invalid_argument("ITEM_WRITE_DENIED");
                Json op{{"id",key},{"action",name=="patch_objects"?"patch":"delete"},{"expected_revision",value.at("revision")}};
                if(name=="patch_objects"){auto patch=value.at("patch");agora::agent::fields(patch);agora::agent::snapGeometry(patch);if(patch.contains("kind")&&patch["kind"]!=item["kind"])throw std::invalid_argument("ITEM_KIND_INVALID");op["patch"]=patch;
                    const auto kind=item.value("kind",std::string());const auto field=kind=="code"?"code":"text";
                    // Match the Python SDK's version-guarded external text edits:
                    // an old CRDT snapshot must not overwrite newly stored source.
                    if((kind=="text"||kind=="note"||kind=="code")&&patch.contains(field))op["unset"]=Json::array({"automerge_snapshot","automerge_changes"});
                }
                operations.push_back(op);ids.push_back(key);
            }
        }else throw std::invalid_argument("AGENT_TOOL_UNKNOWN");
        const auto client="agent-"+std::to_string(identity["user_id"].get<long long>()),key=std::to_string(identity["user_id"].get<long long>())+":"+client;
        const auto sync=memory.brokerSyncState(scope,id,key);if(!sync)throw std::runtime_error("STORAGE_UNAVAILABLE");const auto receipt=sync->value("receipt",Json());const auto count=receipt.is_null()?1LL:receipt.value("count",0LL)+1;
        auto nested=input;nested["event"]={{"type","canvas_ops"},{"client_id",client},{"request_count",count},{"operations",operations}};
        lock.unlock();auto applied=query(nested);
        auto response=applied.at("response");response["type"]="agent_tool_result";response["result"]={{"object_ids",ids},{"revision",response.value("revision",0LL)}};applied["response"]=response;applied["agent_visibility"]=visibility;return applied;
    }
    if(type=="runtime_execute"&&event.value("action",std::string())=="agent"){
        if(scope!=SpaceScope::Canvas||(!admin&&groups.empty()))throw std::invalid_argument("AGENT_SCOPE_DENIED");
        const auto user=identity["user_id"].get<long long>();
        if(!hasAdultAgentAccess(user))throw std::invalid_argument("AGENT_ADULT_ACCESS_REQUIRED");
        const auto operation=event.value("operation",std::string("run"));
        const auto personal_key=event.value("api_key",std::string());
        if(!personal_key.empty()&&(operation!="run"||personal_key.size()>256||personal_key.compare(0,9,"sk-or-v1-")!=0
            ||!std::all_of(personal_key.begin(),personal_key.end(),[](unsigned char c){return std::isalnum(c)||c=='-'||c=='_';})))
            throw std::invalid_argument("AGENT_API_KEY_INVALID");
        const auto clock=[](){return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count();};
        auto sessionCall=[&](const std::string& operation,Json args){
            args["user_id"]=user;args["nickname"]=identity.value("nickname",std::string());args["now"]=clock();args["new_id"]=agora::agent::randomId();
            const auto reply=memory.agentSessions(id,operation,args);if(!reply)throw std::runtime_error("AGENT_SESSION_STORAGE_ERROR");
            if(reply->contains("error"))throw std::invalid_argument((*reply)["error"].get<std::string>());return *reply;
        };
        auto sessionRead=[&](const std::string& session){
            if(!validQueryItemId(session))throw std::invalid_argument("AGENT_SESSION_INVALID");
            auto result=sessionCall("meta",{{"session_id",session}}).at("session");
            if(!agora::agent::sessionVisible(result,user,groups,admin))throw std::invalid_argument("AGENT_SESSION_ACCESS_DENIED");return result;
        };
        auto sessionNotice=[&](long long revision){if(on_output)on_output(Json{{"agent_event","commits"},{"broadcasts",Json::array({{{"all",true},{"event",{{"type","agent_sessions_changed"},{"session_revision",revision}}}}})}}.dump());};
        if(operation=="sessions"){
            const auto offset=event.value("offset",0),limit=event.value("limit",30);if(offset<0||offset>10000000||limit<1||limit>50)throw std::invalid_argument("AGENT_SESSION_INVALID");
            auto page=sessionCall("list",{{"offset",offset},{"limit",limit}});Json sessions=Json::array();
            for(const auto& summary:page["sessions"])if(agora::agent::sessionVisible(summary,user,groups,admin))sessions.push_back(agora::agent::publicSession(summary,user,admin));
            Json result{{"sessions",sessions},{"draft",agora::agent::publicSession(page["draft"],user,admin)},{"next_offset",page["next_offset"]},{"revision",page["revision"]},{"user_id",user}};
            return envelope(route,{{"type","runtime_result"},{"ok",true},{"result",result}});
        }
        if(operation=="session_delete"){
            const auto session=event.value("session_id",std::string());const auto summary=sessionRead(session);
            if(!admin&&summary.value("owner_id",0LL)!=user)throw std::invalid_argument("AGENT_SESSION_DELETE_DENIED");
            const auto result=sessionCall("delete",{{"session_id",session},{"admin",admin}});sessionNotice(result.value("revision",0LL));
            return envelope(route,{{"type","runtime_result"},{"ok",true},{"result",result}});
        }
        if(operation=="session_get"){
            const auto session=event.value("session_id",std::string());sessionRead(session);
            const auto limit=event.value("limit",50);if(limit<1||limit>100)throw std::invalid_argument("AGENT_SESSION_INVALID");Json args{{"session_id",session},{"limit",limit}};
            if(event.contains("before")){if(!event["before"].is_number_integer()||event["before"].get<long long>()<0)throw std::invalid_argument("AGENT_SESSION_INVALID");args["before"]=event["before"];}
            auto result=sessionCall("get",args);if(!agora::agent::sessionVisible(result["session"],user,groups,admin))throw std::invalid_argument("AGENT_SESSION_ACCESS_DENIED");
            result["session"]=agora::agent::publicSession(result["session"],user,admin);if(result["messages"].empty())result["messages"]=Json::array();
            return envelope(route,{{"type","runtime_result"},{"ok",true},{"result",result}});
        }
        if(operation!="run"&&operation!="status")throw std::invalid_argument("AGENT_REQUEST_INVALID");
        const auto message=event.value("message",std::string());
        if(operation=="run"&&(message.empty()||message.size()>24000))throw std::invalid_argument("AGENT_REQUEST_INVALID");
        const auto consent_version=event.value("consent_version",std::string());
        if(operation=="run"&&consent_version!="agent-adult-v3")throw std::invalid_argument("AGENT_CONSENT_REQUIRED");
        const auto agent_options=event.value("options",Json::object());agora::agent::options(agent_options);
        const auto provider=agent_options.value("provider",std::string("anthropic"));
        const auto model=agent_options.value("model",std::string());
        const bool external=provider=="openrouter";
        if(external ? (model.compare(0,17,"anthropic/claude-")!=0 || (operation=="run" && personal_key.empty()))
                    : (provider!="anthropic" || !personal_key.empty()
                       || (!model.empty()&&model!="claude-opus-5-5"&&model!="claude-sonnet-5-5"&&model!="claude-haiku-5-5")))
            throw std::invalid_argument("AGENT_OPTIONS_INVALID");
        std::string session,turn;Json begun;
        if(operation=="run"){
            session=event.value("session_id",std::string());if(session.empty())session=sessionCall("ensure",Json::object())["draft"]["id"].get<std::string>();
            const auto summary=sessionRead(session);if(summary.value("owner_id",0LL)!=user)throw std::invalid_argument("AGENT_SESSION_READ_ONLY");
            turn=event.value("turn_id",route["request_id"].get<std::string>());if(!validQueryItemId(turn))throw std::invalid_argument("AGENT_SESSION_INVALID");
            const auto serialized=Json{{"message",message},{"options",agent_options},{"consent_version",consent_version}}.dump();
            unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(serialized.data()),serialized.size(),hash);const char* hex="0123456789abcdef";std::string digest;for(auto byte:hash){digest+=hex[byte>>4];digest+=hex[byte&15];}
            Json userMessage{{"id",turn},{"turn_id",turn},{"role","user"},{"content",message},{"status","running"},{"created_at",clock()},{"user_id",user},{"nickname",identity.value("nickname",std::string())},{"options",agent_options}};
            begun=sessionCall("begin",{{"session_id",session},{"turn_id",turn},{"digest",digest},{"message",userMessage},{"title",agora::agent::sessionTitle(message)},{"visibility",Json::array()}});
            if(begun.value("duplicate",false)){
                const auto replay=begun["replay"];Json result{{"answer",replay.value("content",std::string())},{"session_id",session},{"next_draft_id",begun["draft"]["id"]},{"duplicate",true}};
                if(replay.value("kind",std::string())=="error"){result["error"]=replay.value("code",std::string("AGENT_FAILED"));result["message"]=replay.value("content",std::string());}
                return envelope(route,{{"type","runtime_result"},{"ok",!result.contains("error")},{"code",result.value("error",std::string())},{"message",result.value("message",std::string())},{"result",result}});
            }
        }
        Json payload{{"options",agent_options},{"operation",operation},{"message",message},{"consent_version",consent_version},{"anchor",event.value("anchor",Json{{"x",.15},{"y",.15}})}};
        if(external && operation=="run"){
            payload["api_key"]=personal_key;
            payload["user_id"]=user;
        }
        lock.unlock();if(operation=="run")sessionNotice(begun.value("revision",0LL));
        auto onTool=[&](const std::string& tool,const Json& args){
            if(tool!="create_objects")return Json{{"error","AGENT_TOOL_DENIED"}};
            auto nested=input;nested["route"]["request_id"]=agora::agent::randomId();nested["event"]={{"type","agent_tool"},{"name",tool},{"arguments",args}};
            auto reply=execute(nested);const auto response=reply.value("response",Json::object());
            if(response.value("ok",true)==false||response.value("type",std::string())=="error")return Json{{"error",response.value("code",std::string("AGENT_TOOL_REJECTED"))}};
            if(reply.contains("broadcasts")&&on_output)on_output(Json{{"agent_event","commits"},{"broadcasts",reply["broadcasts"]}}.dump());
            if(operation=="run"&&reply.contains("agent_visibility")){const auto updated=sessionCall("restrict",{{"session_id",session},{"visibility",reply["agent_visibility"]}});sessionNotice(updated.value("revision",0LL));}
            return response.value("result",Json::object());
        };
        auto onProgress=[&](const std::string& text){if(on_output)on_output(Json{{"agent_event","progress"},{"text",text}}.dump());};
        Json result;
        try{result=agora::agent::request(payload,onTool,onProgress);}
        catch(const std::exception&){result={{"error","AGENT_UNAVAILABLE"},{"message","에이전트 연결이 종료되었습니다. 이미 반영된 변경은 유지됩니다."}};}
        if(operation=="run"){
            const bool failed=result.contains("error");auto content=result.value(failed?"message":"answer",std::string());if(content.size()>240000){auto n=std::size_t(240000);while(n>0&&(static_cast<unsigned char>(content[n])&0xc0)==0x80)--n;content.resize(n);content+="\n[세션 기록 길이 상한으로 일부를 생략했습니다.]";}
            Json answer{{"id",turn+":result"},{"turn_id",turn},{"role","assistant"},{"kind",failed?"error":"answer"},{"content",content},{"status",failed?"failed":"complete"},{"created_at",clock()},{"code",result.value("error",std::string())}};
            const auto finished=sessionCall("finish",{{"session_id",session},{"turn_id",turn},{"status",failed?"failed":"complete"},{"message",answer}});sessionNotice(finished.value("revision",0LL));
            result["session_id"]=session;result["next_draft_id"]=begun["draft"]["id"];
        }
        const bool ok=!result.contains("error");return envelope(route,{{"type","runtime_result"},{"ok",ok},{"code",result.value("error",std::string())},{"message",result.value("message",std::string())},{"result",result}});
    }

    if (type == "runtime_execute") {
        if (!event.contains("action") || !event["action"].is_string()) throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
        const auto action=event["action"].get<std::string>();
        if (action!="execute"&&action!="build"&&action!="notebook"&&action!="manage") throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
        if(action=="manage"){
            const auto management=event.value("management",Json::object());
            if(!management.is_object())throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
            const auto operation=management.value("action",std::string());
            const std::unordered_set<std::string> allowed={"settings_get","settings_set","resources_set","disk_set","java_dependencies","reset"};
            if(!allowed.count(operation))throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
            if(!admin&&(operation=="settings_get"?groups.empty():(write_groups.empty()||execute_groups.empty())))throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            Json payload=management;payload["canvasId"]=runtimeTargetId(scope, id);
            lock.unlock();
            const auto result=RuntimeKernelClient(runtime_socket_).request(payload);
            if(result.contains("error"))return envelope(route,{{"type","runtime_result"},{"ok",false},{"code",result.value("error",std::string("RUNTIME_VM_UNAVAILABLE"))}});
            return envelope(route,{{"type","runtime_result"},{"ok",true},{"result",result}});
        }
        if (execute_groups.empty()&&!admin) throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
        const auto object_id=event.value("object_id",std::string());
        const auto language=event.value("language",std::string("python"));
        const auto code=event.value("code",std::string());
        if(!validQueryItemId(object_id)||code.size()>65536||
           (language!="python"&&language!="javascript"&&language!="c"&&language!="cpp"&&language!="bash"&&language!="java"))
            throw std::invalid_argument("RUNTIME_REQUEST_INVALID");

        Json manager_request{{"canvasId",runtimeTargetId(scope, id)},{"code",code}};
        if(action=="notebook"){
            const auto notebook_id=event.value("notebook_id",std::string());
            const auto cell_id=event.value("cell_id",std::string());
            const auto operation=event.value("operation",std::string("execute"));
            if(operation!="execute"&&operation!="prepare"&&operation!="reset")throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
            if(operation=="prepare"&&write_groups.empty()&&!admin)throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            if(!validQueryItemId(notebook_id)||(operation=="execute"&&!validQueryItemId(cell_id)))throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
            const auto notebook=readItem(memory,scope,id,notebook_id);
            if(!notebook||notebook->value("kind","")!="shape"||notebook->value("runtimeRole","")!="notebook"
               ||!queryCanAccess(*notebook,execute_groups,admin))throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            if(operation!="execute"){
                if(object_id!=notebook_id)throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            }else if(object_id==notebook_id){
                const auto cells=notebook->value("notebookCells",Json::array());
                const bool found=cells.is_array()&&std::any_of(cells.begin(),cells.end(),[&](const Json& cell){return cell.is_object()&&cell.value("id",std::string())==cell_id&&cell.value("cellType",std::string("code"))=="code";});
                if(!found)throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            }else{
                const auto child=readItem(memory,scope,id,object_id);
                const auto child_ids=notebook->value("notebookCellIds",Json::array());
                const bool linked=child_ids.is_array()&&std::find(child_ids.begin(),child_ids.end(),Json(object_id))!=child_ids.end();
                if(!child||!linked||child->value("kind","")!="shape"||child->value("runtimeRole","")!="notebook-cell"
                   ||child->value("notebookId","")!=notebook_id||child->value("notebookCell",Json::object()).value("id",std::string())!=cell_id
                   ||child->value("notebookCell",Json::object()).value("cellType",std::string("code"))!="code"
                   ||!queryCanAccess(*child,execute_groups,admin))throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            }
            manager_request["action"]="notebook";manager_request["operation"]=operation;manager_request["notebook"]=notebook_id;
        }else{
            const auto target=readItem(memory,scope,id,object_id);
            const bool code_object=target&&(target->value("kind","")=="code"||
                (target->value("kind","")=="text"&&target->value("renderMode","")=="code"));
            if(!code_object||!queryCanAccess(*target,execute_groups,admin))throw std::invalid_argument("RUNTIME_EXECUTION_DENIED");
            if (action == "build") manager_request["action"] = action;
            manager_request["language"] = language;
            if(event.contains("stdin")){
                if(!event["stdin"].is_string()||event["stdin"].get_ref<const std::string&>().size()>65536)throw std::invalid_argument("RUNTIME_REQUEST_INVALID");
                manager_request["stdin"]=event["stdin"];
            }
            if(action=="execute"&&on_output)manager_request["stream"]=true;
        }
        try {
            // Kernel execution can be long; keep the canvas available for edits and queries.
            lock.unlock();
            const auto result=RuntimeKernelClient(runtime_socket_).request(manager_request,on_output);
            if(result.contains("error"))return envelope(route,{{"type","runtime_result"},{"ok",false},{"code",result.value("error",std::string("RUNTIME_VM_UNAVAILABLE"))},{"message",result.value("message",std::string("VM 커널에서 실행 결과를 받지 못했습니다."))}});
            return envelope(route,{{"type","runtime_result"},{"ok",true},{"result",result}});
        } catch(const std::runtime_error& error) {
            const std::string code=error.what();
            return envelope(route,{{"type","runtime_result"},{"ok",false},{"code",code},{"message","C++ 브로커가 VM 커널에 코드를 전달하지 못했습니다."}});
        }
    }
    if (type == "canvas_ops" || type == "canvas_sync") {
        const auto client = event.value("client_id", "");
        if (!validQueryItemId(client)) throw std::invalid_argument("CLIENT_ID_INVALID");
        const auto key = std::to_string(identity["user_id"].get<long long>()) + ":" + client;
        const auto sync = memory.brokerSyncState(scope, id, key);
        if (!sync) throw std::runtime_error("STORAGE_UNAVAILABLE");
        const auto receipt = sync->value("receipt", Json());
        const auto accepted = receipt.is_null()?0LL:receipt.value("count",0LL);
        if (type == "canvas_sync") {
            auto raw = memory.loadCanvas(scope, id); if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
            const auto doc = Json::parse(*raw); Json items = Json::object();
            const auto source=doc.value("items",Json::object());
            for (const auto& entry : source.items())
                if (entry.value().value("kind", "") != "host-tombstone" && queryCanAccess(entry.value(),groups,admin)) {
                    auto item=entry.value();if(agora::canvas::isChatRoomItem(item)){item.erase("data");item.erase("requests");}items[entry.key()]=std::move(item);
                }
            return envelope(route, {{"type","canvas_snapshot"},{"edit_protocol",2},{"points_protocol",1},{"sync_protocol",2},{"sync_user_id",identity["user_id"]},{"items",items},{"revision",(*sync)["revision"]},{"client_id",client},{"accepted_count",accepted}});
        }
        if (!event.contains("request_count") || !event["request_count"].is_number_integer()) throw std::invalid_argument("CLIENT_SEQUENCE_INVALID");
        const auto count=event["request_count"].get<long long>();
        if(count<=0 || count>=9007199254740991LL) throw std::invalid_argument("CLIENT_SEQUENCE_INVALID");
        const auto operations=event.value("operations",Json::array());const auto serialized=operations.dump();
        unsigned char hash[SHA256_DIGEST_LENGTH];SHA256(reinterpret_cast<const unsigned char*>(serialized.data()),serialized.size(),hash);
        const char* hex="0123456789abcdef";std::string digest;for(auto byte:hash){digest+=hex[byte>>4];digest+=hex[byte&15];}
        agora::broker::Mutation mutation;
        // Check the durable receipt before replay validation (a repeated create is valid).
        if(count==accepted && !receipt.is_null()){
            if(receipt.value("digest","")!=digest || receipt.value("request_id","")!=route["request_id"].get<std::string>())throw std::invalid_argument("CLIENT_REQUEST_MISMATCH");
        }else{
            if(count!=accepted+1)throw std::invalid_argument("CLIENT_SEQUENCE_CONFLICT");
            if(!operations.is_array()||operations.empty()||operations.size()>512)throw std::invalid_argument("QUERY_BATCH_INVALID");
            std::unordered_set<std::string> wanted;
            for(const auto& operation:operations){
                if(!operation.is_object())throw std::invalid_argument("QUERY_ACTION_INVALID");
                if(operation.value("action","")=="translate"){
                    const auto ids=operation.value("ids",Json::array());
                    if(!ids.is_array()||ids.empty()||ids.size()>10000)throw std::invalid_argument("ITEM_TRANSLATION_INVALID");
                    for(const auto& item:ids){if(!item.is_string()||!validQueryItemId(item.get<std::string>()))throw std::invalid_argument("ITEM_ID_INVALID");wanted.insert(item.get<std::string>());}
                }else{
                    const auto item=operation.value("id","");if(!validQueryItemId(item))throw std::invalid_argument("ITEM_ID_INVALID");wanted.insert(item);
                }
                if(wanted.size()>20000)throw std::invalid_argument("QUERY_BATCH_INVALID");
            }
            const auto loaded=memory.readBrokerItems(scope, id, std::vector<std::string>(wanted.begin(),wanted.end()));
            if(!loaded)throw std::runtime_error("STORAGE_UNAVAILABLE");
            mutation=agora::broker::prepare(operations,[&](const std::string& item)->std::optional<Json>{
                if(loaded->contains(item))return (*loaded)[item].is_null()?std::nullopt:std::optional<Json>((*loaded)[item]);
                return readItem(memory,scope,id,item);
            },write_groups,admin,&groups,&execute_groups,&member_groups);
        }
        // Activity counts the user's own edits only, before comment rooms are added.
        const Json activity=scope==SpaceScope::Canvas?agora::activity::delta(mutation.previous,mutation.items):Json::object();
        // Deleting an object (including a chat channel) deletes its comment thread too.
        Json purged_rooms=Json::array();
        {
            std::vector<std::string> rooms;
            for(const auto& entry:mutation.items.items()){
                if(!entry.value().is_null())continue;
                const auto old=mutation.previous.value(entry.key(),Json());
                if(!old.is_object()||agora::canvas::isChatRoomItem(old))continue;
                rooms.push_back(commentRoomId(entry.key()));
            }
            if(!rooms.empty()){
                const auto existing=memory.readBrokerItems(scope, id, rooms);
                if(!existing)throw std::runtime_error("STORAGE_UNAVAILABLE");
                for(const auto& room:rooms){
                    if(!existing->contains(room)||!agora::canvas::isChatRoomItem((*existing)[room]))continue;
                    mutation.previous[room]=(*existing)[room];
                    mutation.items[room]=nullptr;
                    purged_rooms.push_back(room);
                }
            }
        }
        const auto result=memory.applyBrokerBatch(scope, id, key, count, route["request_id"], digest, mutation.items, mutation.expected, activity, purged_rooms);
        if(!result)throw std::runtime_error("QUERY_OUTCOME_UNKNOWN");
        if(result->contains("error"))throw std::invalid_argument((*result)["error"].get<std::string>());
        Json response=*result;response["type"]="canvas_result";response["ok"]=true;response["client_id"]=client;response["request_count"]=count;
        Json broadcasts=Json::array();
        if(!result->value("duplicate",false)){
            const auto revision=(*result)["revision"];Json permissions=Json::object(),previous=Json::object();
            for(auto& entry:mutation.items.items()){
                const auto old=mutation.previous[entry.key()];previous[entry.key()]=old.is_null()?Json::array():old.value("permission",Json::array());
                permissions[entry.key()]=entry.value().is_null()?Json::array():entry.value().value("permission",Json::array());
                if(!entry.value().is_null())entry.value()["__broker_revision"]=revision;
            }
            Json commit{{"type","canvas_commit"},{"author_id",identity["user_id"]},{"revision",revision},{"client_id",client},{"request_count",count},{"request_id",route["request_id"]},{"items",mutation.items}};
            Json deltas=Json::object();
            for(const auto& entry:mutation.items.items()){
                const auto old=mutation.previous.value(entry.key(),Json());
                if(old.is_object()&&entry.value().is_object()){
                    auto delta=Json::diff(old,entry.value());
                    for(auto& edit:delta)if(edit.value("op",std::string())=="replace"&&edit.contains("value")&&edit["value"].is_string()){
                        const Json::json_pointer pointer(edit["path"].get<std::string>());
                        if(old.contains(pointer)&&old[pointer].is_string()){
                            auto text=agora::textdelta::diff(old[pointer].get<std::string>(),edit["value"].get<std::string>());
                            if(text.dump().size()<edit["value"].dump().size()){edit["op"]="text";edit["value"]=text;}
                        }
                    }
                    deltas[entry.key()]=delta;
                }
            }
            if(!deltas.empty())commit["deltas"]=deltas;
            if(mutation.only_translations){commit.erase("deltas");commit.erase("items");commit["translations"]=mutation.translations;}
            broadcasts.push_back({{"all",true},{"event",commit},{"permissions",permissions},{"previous_permissions",previous}});
        }else broadcasts.push_back({{"all",true},{"event",{{"type","canvas_watermark"},{"revision",(*result)["server_revision"]}}}});
        return envelope(route,response,broadcasts);
    }
    if (type == "broker_lease") return envelope(route, {{"type", "lease_result"}, {"ok", true}});
    if (type == "workspace_ops" || type == "workspace_sync") {
        const auto workspace_id = event.value("workspace_id", "");
        if (!validQueryItemId(workspace_id)) throw std::invalid_argument("WORKSPACE_ID_INVALID");
        if (type == "workspace_ops" && write_groups.empty() && !admin) throw std::invalid_argument("ITEM_WRITE_DENIED");
        const auto cells = event.value("cells", Json::array());
        if (!cells.is_array() || cells.size() > 64 || cells.dump().size() > 524288)
            throw std::invalid_argument("ITEM_NOTEBOOK_INVALID");
        Json commit = {
            {"type", "workspace_commit"},
            {"canvas_id", id},
            {"workspace_id", workspace_id},
            {"title", event.value("title", "")},
            {"author_id", identity["user_id"]},
            {"client_id", event.value("client_id", "")},
            {"action", event.value("action", type == "workspace_sync" ? "sync" : "update")},
            {"cells", cells},
            {"results", event.value("results", Json::object())}
        };
        Json broadcasts = Json::array({
            {{"all", true}, {"event", commit}, {"permission", Json(groups)}}
        });
        return envelope(route, {{"type", "workspace_result"}, {"ok", true}, {"workspace_id", workspace_id}}, broadcasts);
    }
    if (type.rfind("canvas_settings_", 0) == 0) {
        auto result = CanvasSettingsService::execute(pool_, canvas, id, identity["user_id"], identity["nickname"], identity["tag_number"], event, route["request_id"]);
        Json broadcasts = Json::array();
        auto reply = envelope(route, result.response, broadcasts);
        if (result.changed) {
            reply["refresh_sessions"] = true;
            const auto field = event.value("field", "");
            reply["refresh_access"] = field != "name" && field != "description" && field != "password";
            reply["refresh_items"] = false;
        }
        return reply;
    }
    if (type == "mentions_get" || type == "mention_read") {
        const auto user_id = identity["user_id"].get<long long>();
        const auto states = memory.readMentionStates(scope, id, user_id);
        if (!states || !states->is_object()) throw std::runtime_error("STORAGE_UNAVAILABLE");
        auto accessible = [&](const Json& note) {
            const auto object_id = note.value("object_id", "");
            const auto key = object_id.empty() ? note.value("room_id", "") : object_id;
            if (!validQueryItemId(key)) return false;
            const auto target = readItem(memory, scope, id, key);
            return target && target->value("kind", "") != "host-tombstone" && queryCanAccess(*target, groups, admin);
        };
        if (type == "mention_read") {
            const auto mention_id = event.value("mention_id", "");
            if (!states->contains(mention_id) || !accessible((*states)[mention_id])) throw std::invalid_argument("MENTION_NOT_FOUND");
            if (!memory.storeMentionState(scope, id, user_id, mention_id, (*states)[mention_id], false) || !memory.flushCanvas(scope, id)) throw std::runtime_error("STORAGE_UNAVAILABLE");
            return envelope(route, {{"type", "mention_read"}, {"ok", true}, {"mention_id", mention_id}},
                Json::array({{{"user_ids", Json::array({user_id})}, {"event", {{"type", "mentions_changed"}}}}}));
        }
        std::vector<Json> candidates;
        for (const auto& entry : states->items()) if (entry.value().is_object() && entry.value().value("unread", false)) candidates.push_back(entry.value());
        std::sort(candidates.begin(), candidates.end(), [](const Json& a, const Json& b) {
            if (a.value("created_at", 0LL) != b.value("created_at", 0LL)) return a.value("created_at", 0LL) > b.value("created_at", 0LL);
            return a.value("mention_id", "") > b.value("mention_id", "");
        });
        Json notifications = Json::array();
        for (const auto& note : candidates) {
            if (accessible(note)) notifications.push_back(note);
            if (notifications.size() >= 100) break;
        }
        return envelope(route, {{"type", "mentions_snapshot"}, {"notifications", notifications}});
    }
    if (type == "chat" || type == "chat_history") {
        const auto object_id = event.value("object_id", "");
        auto room_id = event.value("room_id", "");
        std::optional<Json> target;
        if (!object_id.empty()) {
            if (!validQueryItemId(object_id)) throw std::invalid_argument("ITEM_ID_INVALID");
            target = readItem(memory, scope, id, object_id);
            if (!target) throw std::invalid_argument("ITEM_ACCESS_DENIED");
            queryCommentPermission(*target, groups, admin);
            room_id = commentRoomId(object_id);
        } else if (room_id.rfind("object-comment-", 0) == 0) {
            throw std::invalid_argument("ITEM_ACCESS_DENIED");
        }
        if (!validQueryItemId(room_id)) throw std::invalid_argument("CHAT_INVALID_ROOM");
        auto room = readItem(memory, scope, id, room_id);
        if (room && (!agora::canvas::isChatRoomItem(*room) || (!target && !queryCanAccess(*room, groups, admin)))) throw std::invalid_argument("ITEM_ACCESS_DENIED");
        if (type == "chat_history") {
            if (!room) {
                Json response{{"type", "chat_history"}, {"room_id", room_id}, {"messages", Json::array()}, {"total", 0}, {"has_more", false}};
                if (target) response["object_id"] = object_id;
                return envelope(route, response);
            }
            std::optional<std::uint64_t> from, to;
            if (event.contains("from_sequence") && event["from_sequence"].is_number_integer() && event["from_sequence"].get<long long>() > 0) from = event["from_sequence"].get<std::uint64_t>();
            if (event.contains("to_sequence") && event["to_sequence"].is_number_integer() && event["to_sequence"].get<long long>() > 0) to = event["to_sequence"].get<std::uint64_t>();
            auto page = memory.readChatPage(scope, id, room_id, from, to, event.value("limit", 50ULL));
            if (!page) throw std::runtime_error("STORAGE_UNAVAILABLE");
            auto response = Json::parse(*page); response["type"] = "chat_history"; response["room_id"] = room_id;
            if (target) response["object_id"] = object_id;
            if (response.contains("from_sequence")) response["next_to_sequence"] = response["from_sequence"].get<long long>() - 1;
            if (response.contains("messages")) for (auto& message : response["messages"]) { message.erase("sender_user_id"); message.erase("mentions"); }
            return envelope(route, response);
        }
        const auto text = event.value("text", ""); if (text.empty() || text.size() > 4096 || groups.empty()) throw std::invalid_argument("CHAT_INVALID_MESSAGE");
        Json permission = target ? queryCommentPermission(*target, groups, admin) : room ? (*room)["permission"] : Json(groups);
        if(!queryCanAccess(Json{{"permission",permission}},write_groups,admin))throw std::invalid_argument("ITEM_WRITE_DENIED");
        const auto sequence = room ? agora::canvas::chatRoomNextSequence(*room) : 1ULL;
        Json message{{"type", "chat"}, {"room_id", room_id}, {"text", text}, {"sender", identity["nickname"]},
            {"tag_number", identity["tag_number"]}, {"sender_user_id", identity["user_id"]}, {"sequence", sequence},
            {"created_at", std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch()).count()},
            {"request_id", route["request_id"]}};
        if (target) message["object_id"] = object_id;
        message["mentions"] = Json::object();
        const auto handles = parseMentionHandles(text);
        if (!handles.empty()) {
            const auto raw = memory.loadCanvas(scope, id);
            if (!raw) throw std::runtime_error("STORAGE_UNAVAILABLE");
            const auto doc = Json::parse(*raw);
            RegistryServiceMemory registry(pool_.getDbHost(), pool_.getDbPort());
            for (const auto& [nickname, tag] : handles) {
                const auto resolved = registry.getActiveUserIdChecked(nickname, tag);
                if (!resolved) throw std::runtime_error("STORAGE_UNAVAILABLE");
                const auto recipient = *resolved;
                if (recipient <= 0 || recipient == identity["user_id"].get<int>()) continue;
                if (!canvasIsPublic(doc) && !canvasHasMember(doc, recipient)) continue;
                const auto [recipient_groups, recipient_admin] = groupsForUser(doc, recipient);
                if (queryCanAccess(Json{{"permission", permission}}, recipient_groups, recipient_admin)) message["mentions"][std::to_string(recipient)] = true;
            }
        }
        const auto committed = memory.commitChatMessage(scope, id, room_id, permission, message);
        if (!committed || !memory.flushCanvas(scope, id)) throw std::runtime_error("STORAGE_UNAVAILABLE");
        message = *committed;
        Json recipients = Json::array();
        const auto recipient_states = message.value("mentions", Json::object());
        for (const auto& entry : recipient_states.items()) recipients.push_back(std::stoll(entry.key()));
        message.erase("sender_user_id"); message.erase("mentions");
        Json broadcasts = Json::array({{{"event", message}, {"permission", permission}}});
        if (!room) broadcasts.insert(broadcasts.begin(), queryChatRoomBroadcast(room_id, permission, sequence + 1));
        if (!recipients.empty()) broadcasts.push_back({{"user_ids", recipients}, {"event", {{"type", "mentions_changed"}}}});
        return envelope(route, {{"type", "event_ack"}, {"ok", true}, {"event_type", type}}, broadcasts);
    }
    const bool batch = type == "host_item_batch" || type == "crud_batch";
    Json operations = batch ? event.value("changes", Json::array()) : Json::array({event});
    if (!operations.is_array() || operations.empty() || operations.size() > 512) throw std::invalid_argument("QUERY_BATCH_INVALID");
    Json prepared = Json::array(), broadcasts = Json::array(), results = Json::array();
    Json purged_rooms = Json::array();
    std::unordered_set<std::string> touched;
    for (auto operation : operations) {
        const auto item_id = operation.value("item_id", operation.value("id", ""));
        if (!validQueryItemId(item_id) || !touched.insert(item_id).second) throw std::invalid_argument("ITEM_ID_INVALID");
        auto previous = readItem(memory, scope, id, item_id);
        const Json old = previous.value_or(Json());
        if (previous && !queryCanAccess(old, groups, admin)) throw std::invalid_argument("ITEM_ACCESS_DENIED");
        auto action = operation.value("action", ""); const auto operation_type = operation.value("type", "");
        if (action.empty()) action = operation_type == "item_delete" || operation_type == "delete_item" ? "delete" : "update";
        if (action == "read") {
            results.push_back({{"item_id", item_id}, {"item", old}}); continue;
        }
        if (action == "delete") {
            if (!queryCanAccess(old,write_groups,admin)) throw std::invalid_argument("ITEM_WRITE_DENIED");
            if (queryObjectLocked(old)) throw std::invalid_argument("ITEM_LOCKED");
            if (previous && agora::canvas::isChatRoomItem(old)) throw std::invalid_argument("CHAT_QUERY_REQUIRED");
            prepared.push_back({{"type", "item_delete"}, {"item_id", item_id}});
            broadcasts.push_back({{"event", {{"type", "item_delete"}, {"item_id", item_id}}}, {"permission", previous ? old.value("permission", Json::array()) : Json::array()}});
            // The object's comment thread (a chat channel's messages) goes with it.
            if (previous && !agora::canvas::isChatRoomItem(old)) {
                const auto room = commentRoomId(item_id);
                const auto thread = readItem(memory, scope, id, room);
                if (thread && agora::canvas::isChatRoomItem(*thread)) {
                    prepared.push_back({{"type", "item_delete"}, {"item_id", room}});
                    broadcasts.push_back({{"event", {{"type", "item_delete"}, {"item_id", room}}}, {"permission", thread->value("permission", Json::array())}});
                    purged_rooms.push_back(room);
                }
            }
        } else if (action == "create" || action == "update" || action == "patch") {
            if (action == "create" && previous) throw std::invalid_argument("ITEM_ALREADY_EXISTS");
            if (action == "patch" && !previous) throw std::invalid_argument("ITEM_NOT_FOUND");
            Json item = operation.value("item", Json::object());
            if (action == "patch") {
                item = old; auto patch = operation.value("patch", Json::object());
                if (!patch.is_object()) throw std::invalid_argument("ITEM_PATCH_INVALID");
                item.update(patch);
                for (const auto& field : operation.value("unset", Json::array())) { if (!field.is_string()) throw std::invalid_argument("ITEM_PATCH_INVALID"); item.erase(field.get<std::string>()); }
            }
            if(item.value("kind","")=="question"){agora::question::validateRevision(old,item);agora::question::validate(item);}
            item = queryUpdatedItem(old, item, write_groups, admin,&member_groups);
            prepared.push_back({{"type", operation_type == "item_save" ? "item_save" : "item_update"}, {"item_id", item_id}, {"item", item}});
            // Host sessions already broadcast accepted object changes through their data channels.
            if (type != "host_item_batch") broadcasts.push_back({{"event", {{"type", "item_update"}, {"item_id", item_id}, {"item", item}}}, {"permission", item["permission"]}});
        } else throw std::invalid_argument("QUERY_ACTION_INVALID");
    }
    std::size_t applied = 0;
    for (const auto& operation : prepared) {
        if (!CanvasEventMemory::apply(canvas, operation)) return envelope(route,
            {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", false}, {"code", "STORAGE_UNAVAILABLE"}, {"applied", applied}});
        ++applied;
    }
    if (!memory.purgeMentionStates(scope, id, purged_rooms)) return envelope(route, {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", false}, {"code", "STORAGE_UNAVAILABLE"}, {"applied", applied}});
    if (!memory.flushCanvas(scope, id)) return envelope(route, {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", false}, {"code", "STORAGE_UNAVAILABLE"}, {"applied", applied}});
    return envelope(route, {{"type", type == "host_item_batch" ? "host_batch_result" : "crud_result"}, {"ok", true}, {"applied", applied}, {"results", results}}, broadcasts);
}
