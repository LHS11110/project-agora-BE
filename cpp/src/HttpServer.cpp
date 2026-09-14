#include "HttpServer.hpp"
#include <iostream>
#include <nlohmann/json.hpp>

HttpServer::HttpServer(CanvasPool& canvas_pool, const std::string& host, int port)
    : canvas_pool_(canvas_pool), host_(host), port_(port) {
    setupRoutes();
}

HttpServer::~HttpServer() {
    stop();
}

void HttpServer::start() {
    std::cout << "[HttpServer] Starting Agora C++ Server on " << host_ << ":" << port_ << "...\n";
    server_.listen(host_.c_str(), port_);
}

void HttpServer::stop() {
    server_.stop();
}

bool HttpServer::registerToken(int user_id, const std::string& token) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    token_to_user_[token] = user_id;
    user_to_token_[user_id] = token;
    std::cout << "[HttpServer] Registered JWT token for user #" << user_id << "\n";
    return true;
}

int HttpServer::authenticateToken(const std::string& token) {
    std::lock_guard<std::mutex> lock(auth_mutex_);
    auto it = token_to_user_.find(token);
    if (it != token_to_user_.end()) {
        return it->second;
    }
    return -1;
}

void HttpServer::setupRoutes() {
    // 1. JWT 토큰 등록 API (POST /api/auth/token)
    server_.Post("/api/auth/token", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            auto body = nlohmann::json::parse(req.body);
            int user_id = body.value("user_id", -1);
            std::string token = body.value("token", "");

            if (user_id <= 0 || token.empty()) {
                res.status = 400;
                res.set_content("{\"error\":\"Invalid user_id or token\"}", "application/json");
                return;
            }

            registerToken(user_id, token);
            res.status = 200;
            res.set_content("{\"status\":\"success\",\"message\":\"Token registered\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 2. Access API (POST /api/access)
    server_.Post("/api/access", [this](const httplib::Request& req, httplib::Response& res) {
        try {
            int canvas_id = 0;
            int user_id = -1;
            std::string token = "";

            if (!req.body.empty()) {
                auto body = nlohmann::json::parse(req.body);
                canvas_id = body.value("canvas_id", 0);
                if (canvas_id == 0) canvas_id = body.value("canvas-id", 0);
                user_id = body.value("user_id", -1);
                token = body.value("token", "");
            }

            if (canvas_id == 0 && req.has_param("canvas_id")) {
                canvas_id = std::stoi(req.get_param_value("canvas_id"));
            }

            if (token.empty()) {
                auto auth_header = req.get_header_value("Authorization");
                if (auth_header.rfind("Bearer ", 0) == 0) {
                    token = auth_header.substr(7);
                }
            }

            // JWT 토큰 인증 수행
            int auth_uid = authenticateToken(token);
            if (auth_uid > 0) {
                user_id = auth_uid;
            } else if (user_id <= 0) {
                res.status = 401;
                res.set_content("{\"error\":\"Invalid or unregistered JWT token\"}", "application/json");
                return;
            }

            if (canvas_id <= 0) {
                res.status = 400;
                res.set_content("{\"error\":\"canvas_id is required\"}", "application/json");
                return;
            }

            // 캔버스 풀에서 캔버스 선택 또는 생성 (Redis 캐싱 포함)
            auto canvas = canvas_pool_.getOrCreateCanvas(canvas_id);
            if (!canvas) {
                res.status = 500;
                res.set_content("{\"error\":\"Failed to initialize canvas in pool\"}", "application/json");
                return;
            }

            // RX 및 TX 포트 쌍 할당
            auto [rx_port, tx_port] = canvas_pool_.allocatePortPair();

            // 사용자 소켓 생성 및 캔버스 풀 연결
            canvas->connectUser(user_id, rx_port, tx_port);

            nlohmann::json resp = {
                {"status", "success"},
                {"canvas_id", canvas_id},
                {"user_id", user_id},
                {"rx_port", rx_port},
                {"tx_port", tx_port}
            };

            res.status = 200;
            res.set_content(resp.dump(), "application/json");
        } catch (const std::exception& e) {
            res.status = 500;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.1 캔버스 이름 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/name)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\",\"message\":\"Canvas not in pool\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string name = body.value("canvas_name", "");

            // 1. Redis 갱신
            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                doc["canvas-name"] = name;
            });

            // 2. 런타임 갱신 및 접속자 브로드캐스트
            canvas->setCanvasName(name);
            nlohmann::json notify = {
                {"type", "canvas_name_updated"},
                {"canvas_id", canvas_id},
                {"canvas_name", name}
            };
            canvas->broadcast(notify);

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.2 캔버스 소유 아이디 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/owner)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            int old_owner = body.value("old_owner_id", 0);
            int new_owner = body.value("new_owner_id", 0);

            // 1. Redis 갱신
            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                doc["admin-user-id"] = new_owner;
            });

            canvas->setAdminUserId(new_owner);

            // 2. 이전/이후 소유자 새로고침 요구
            nlohmann::json refresh_msg = {
                {"type", "refresh_required"},
                {"reason", "owner_changed"},
                {"canvas_id", canvas_id},
                {"new_owner_id", new_owner}
            };
            if (old_owner > 0) canvas->sendToUser(old_owner, refresh_msg);
            if (new_owner > 0) canvas->sendToUser(new_owner, refresh_msg);

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.3 설명 텍스트 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/description)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string desc = body.value("description", "");

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                doc["description"] = desc;
            });

            nlohmann::json notify = {
                {"type", "description_updated"},
                {"canvas_id", canvas_id},
                {"description", desc}
            };
            canvas->broadcast(notify);

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.4 캔버스 비밀번호 즉시 반영 API (모든 사용자 재접속 요구)
    server_.Post(R"(/api/canvas/(\d+)/reflect/password)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string pw_hash = body.value("password_hash", "");

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                doc["canvas-password-hash"] = pw_hash.empty() ? nullptr : nlohmann::json(pw_hash);
            });

            // 접속 중인 모든 사용자에게 재접속 요구 알림 전송 후 소켓 종료
            nlohmann::json reconnect_msg = {
                {"type", "reconnect_required"},
                {"reason", "password_changed"},
                {"canvas_id", canvas_id}
            };
            canvas->broadcast(reconnect_msg);
            canvas->disconnectAll();

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.5 초대된 사용자 리스트 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/people)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string action = body.value("action", "");
            int user_id = body.value("user_id", 0);

            // Redis 갱신
            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                if (action == "remove") {
                    if (doc.contains("people") && doc["people"].is_array()) {
                        auto& p = doc["people"];
                        for (auto it = p.begin(); it != p.end();) {
                            if (it->is_number_integer() && it->get<int>() == user_id) {
                                it = p.erase(it);
                            } else {
                                ++it;
                            }
                        }
                    }
                    if (doc.contains("inner-group") && doc["inner-group"].is_object()) {
                        for (auto& [grp, uids] : doc["inner-group"].items()) {
                            if (uids.is_array()) {
                                for (auto it = uids.begin(); it != uids.end();) {
                                    if (it->is_number_integer() && it->get<int>() == user_id) {
                                        it = uids.erase(it);
                                    } else {
                                        ++it;
                                    }
                                }
                            }
                        }
                    }
                } else if (action == "add") {
                    if (doc.contains("people") && doc["people"].is_array()) {
                        doc["people"].push_back(user_id);
                    }
                }
            });

            // 사용자가 제거된 경우 접속 중이라면 즉시 접속 중단
            if (action == "remove" && canvas->isUserActive(user_id)) {
                nlohmann::json msg = {
                    {"type", "access_revoked"},
                    {"reason", "removed_from_people"},
                    {"canvas_id", canvas_id}
                };
                canvas->sendToUser(user_id, msg);
                canvas->disconnectUser(user_id);
            }

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.6 내부 그룹 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/inner-group)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string action = body.value("action", "");
            std::string group_name = body.value("group_name", "");

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                if (action == "remove") {
                    if (doc.contains("inner-group") && doc["inner-group"].is_object()) {
                        doc["inner-group"].erase(group_name);
                    }
                } else if (action == "add") {
                    if (doc.contains("inner-group") && doc["inner-group"].is_object()) {
                        if (!doc["inner-group"].contains(group_name)) {
                            doc["inner-group"][group_name] = nlohmann::json::array();
                        }
                    }
                }
            });

            // 그룹이 제거된 경우 해당 그룹의 사용자들은 새로고침 요구
            if (action == "remove" && body.contains("affected_users") && body["affected_users"].is_array()) {
                nlohmann::json refresh_msg = {
                    {"type", "refresh_required"},
                    {"reason", "group_removed"},
                    {"group_name", group_name},
                    {"canvas_id", canvas_id}
                };
                for (auto& uid : body["affected_users"]) {
                    if (uid.is_number_integer()) {
                        canvas->sendToUser(uid.get<int>(), refresh_msg);
                    }
                }
            }

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.7 그룹에 속한 사용자 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/group-member)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string action = body.value("action", "");
            std::string group_name = body.value("group_name", "");
            int user_id = body.value("user_id", 0);

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                if (doc.contains("inner-group") && doc["inner-group"].contains(group_name)) {
                    auto& members = doc["inner-group"][group_name];
                    if (members.is_array()) {
                        if (action == "remove") {
                            for (auto it = members.begin(); it != members.end();) {
                                if (it->is_number_integer() && it->get<int>() == user_id) {
                                    it = members.erase(it);
                                } else {
                                    ++it;
                                }
                            }
                        } else if (action == "add") {
                            members.push_back(user_id);
                        }
                    }
                }
            });

            // 그룹에서 제거된 사용자는 새로고침 요구
            if (action == "remove" && user_id > 0) {
                nlohmann::json refresh_msg = {
                    {"type", "refresh_required"},
                    {"reason", "removed_from_group"},
                    {"group_name", group_name},
                    {"canvas_id", canvas_id}
                };
                canvas->sendToUser(user_id, refresh_msg);
            }

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 3.8 초기 그룹 즉시 반영 API
    server_.Post(R"(/api/canvas/(\d+)/reflect/init-group)", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        auto canvas = canvas_pool_.getCanvas(canvas_id);
        if (!canvas) {
            res.status = 200;
            res.set_content("{\"status\":\"ignored\"}", "application/json");
            return;
        }

        try {
            auto body = nlohmann::json::parse(req.body);
            std::string old_group = body.value("old_group", "");
            std::string new_group = body.value("new_group", "");

            RedisClient redis(canvas->getRedisIp(), canvas->getRedisPort());
            redis.updateJson("canvas:" + std::to_string(canvas_id), [&](nlohmann::json& doc) {
                doc["init-group"] = new_group;
            });

            // 변경 전 그룹과 후 그룹 모두에 속한 사용자들은 새로고침 요구
            if (body.contains("affected_users") && body["affected_users"].is_array()) {
                nlohmann::json refresh_msg = {
                    {"type", "refresh_required"},
                    {"reason", "init_group_changed"},
                    {"old_group", old_group},
                    {"new_group", new_group},
                    {"canvas_id", canvas_id}
                };
                for (auto& uid : body["affected_users"]) {
                    if (uid.is_number_integer()) {
                        canvas->sendToUser(uid.get<int>(), refresh_msg);
                    }
                }
            }

            res.status = 200;
            res.set_content("{\"status\":\"success\"}", "application/json");
        } catch (const std::exception& e) {
            res.status = 400;
            res.set_content(std::string("{\"error\":\"") + e.what() + "\"}", "application/json");
        }
    });

    // 사용자 연결 중단 API (Spring 회원 삭제 시 호출)
    server_.Post(R"(/api/users/(\d+)/disconnect)", [this](const httplib::Request& req, httplib::Response& res) {
        int user_id = std::stoi(req.matches[1]);
        canvas_pool_.disconnectUserFromAll(user_id);
        res.status = 200;
        res.set_content("{\"status\":\"success\",\"message\":\"User disconnected\"}", "application/json");
    });

    // 캔버스 제거 API (Spring 캔버스 삭제 시 호출)
    server_.Delete(R"(/api/canvas/(\d+))", [this](const httplib::Request& req, httplib::Response& res) {
        int canvas_id = std::stoi(req.matches[1]);
        bool removed = canvas_pool_.removeCanvas(canvas_id);
        res.status = 200;
        nlohmann::json r = {{"status", "success"}, {"canvas_id", canvas_id}, {"removed", removed}};
        res.set_content(r.dump(), "application/json");
    });

    // 활성 캔버스 수 조회 API (로드 밸런서 측정용)
    server_.Get("/api/canvas/count", [this](const httplib::Request& req, httplib::Response& res) {
        int count = canvas_pool_.getActiveCanvasCount();
        nlohmann::json r = {{"status", "success"}, {"count", count}};
        res.status = 200;
        res.set_content(r.dump(), "application/json");
    });

    // 헬스체크
    server_.Get("/health", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"UP\",\"service\":\"Agora C++ Realtime Server\"}", "application/json");
    });

    server_.Get("/", [](const httplib::Request& req, httplib::Response& res) {
        res.status = 200;
        res.set_content("{\"status\":\"online\",\"service\":\"Agora C++ Server\"}", "application/json");
    });
}
