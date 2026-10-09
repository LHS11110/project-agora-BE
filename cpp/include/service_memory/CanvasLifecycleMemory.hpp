#pragma once

#include <unordered_map>
#include <vector>
#include <memory>
#include <mutex>
#include <atomic>
#include <cstdint>
#include <optional>
#include "Canvas.hpp"
#include "service_memory/CanvasSnapshotMemory.hpp"
#include "service_memory/RegistryServiceMemory.hpp"

class CanvasLifecycleMemory {
public:

    CanvasLifecycleMemory(const std::string& db_host = "127.0.0.1", int db_port = 1433,
               const std::string& es_host = "127.0.0.1", int es_port = 9200,
               const std::string& java_host = "127.0.0.1", int java_port = 8080,
               const std::string& cpp_server_ip = "127.0.0.1", int cpp_server_port = 8000);
    ~CanvasLifecycleMemory();

    std::shared_ptr<Canvas> getCanvas(int canvas_id);

    // Check participant and settings-revision access before any cache allocation
    // or user-session update.
    bool isCanvasAccessAuthorized(int canvas_id, int user_id, long long settings_revision);
    std::optional<nlohmann::json> getAuthorizedCanvasDocument(
        int canvas_id, int user_id, long long settings_revision);

    // Load/touch query state only. Broker owns every user connection.
    std::shared_ptr<Canvas> loadForQuery(int canvas_id);

    // Remove canvas from pool, close sockets, clean up Redis, reflect to ES, update MSSQL
    bool removeCanvas(int canvas_id);

    // Freeze and persist every loaded canvas before notifying clients of a server restart.
    bool saveCanvasesForShutdown();

    int getActiveCanvasCount();
    std::vector<int> getActiveCanvasIds();

    // Check DB every 30 mins and unload if no users
    void cleanupInactiveCanvases();

    std::string getJavaHost() const { return java_host_; }
    int getJavaPort() const { return java_port_; }

    std::string getDbHost() const { return db_host_; }
    int getDbPort() const { return db_port_; }
    std::string getEsHost() const { return es_host_; }
    int getEsPort() const { return es_port_; }
    std::string getCppServerIp() const { return cpp_server_ip_; }
    int getCppServerPort() const { return cpp_server_port_; }

private:
    std::shared_ptr<std::mutex> lifecycleMutexForCanvas(int canvas_id);
    std::shared_ptr<Canvas> getOrCreateCanvasWithLifecycleLock(int canvas_id);
    bool removeCanvasImpl(int canvas_id);
    bool saveCanvasSnapshotToElasticsearch(
        int canvas_id, const std::shared_ptr<Canvas>& canvas,
        std::unique_lock<std::mutex>& settings_lock, std::string& cache_generation);
    bool unloadCanvas(int canvas_id, std::shared_ptr<Canvas> canvas);

    std::unordered_map<int, std::shared_ptr<Canvas>> canvases_;
    // Serializes initialization and unload for each canvas ID.
    std::unordered_map<int, std::shared_ptr<std::mutex>> lifecycle_mutexes_;
    std::mutex pool_mutex_;
    std::string db_host_;
    int db_port_;
    std::string es_host_;
    int es_port_;
    std::string java_host_;
    int java_port_;
    std::string cpp_server_ip_;
    int cpp_server_port_;

};
