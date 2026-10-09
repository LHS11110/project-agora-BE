#include "service/CanvasPersistenceService.hpp"
#include "service_memory/CanvasEventMemory.hpp"
#include "RequestLogContext.hpp"
#include "ElasticsearchBulkLogBuffer.hpp"
#include <iostream>

void CanvasPersistenceService::drain(const std::shared_ptr<Canvas>& canvas) {
    nlohmann::json event;
    std::uint64_t ticket = 0;
    std::string request_id;
    std::string parent_request_id;
    while (canvas && canvas->nextPersistence(event, ticket, &request_id, &parent_request_id)) {
        const std::string event_type = event.is_object() ? event.value("type", "unknown") : "unknown";
        agora::logging::RequestLogContextScope request_context(
            request_id.empty() ? agora::logging::generateRequestId() : request_id,
            "canvas.persistence." + event_type, parent_request_id);
        bool succeeded = false;
        try {
            succeeded = CanvasEventMemory::apply(canvas, event);
        } catch (const std::exception& e) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event: " << e.what() << "\n";
        } catch (...) {
            std::cerr << "[uWebSockets] Failed to persist canvas item event\n";
        }
        if (!succeeded) {
            std::cerr << "[uWebSockets] Canvas #" << canvas->getCanvasId()
                      << " Redis write failed; preventing Elasticsearch snapshot\n";
        }
        ElasticsearchBulkLogBuffer::instance().record(
            "canvas_persistence", "canvas_persistence", succeeded ? "INFO" : "ERROR",
            succeeded ? "Canvas event persisted to Redis" : "Canvas event persistence failed",
            {{"canvas_id", canvas->getCanvasId()}, {"event_type", event_type}, {"ticket", ticket}},
            succeeded ? "success" : "failure", succeeded ? "" : "REDIS_WRITE_FAILED");
        canvas->endPersistence(ticket, succeeded);
    }
}

