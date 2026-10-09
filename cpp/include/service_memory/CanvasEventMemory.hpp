#pragma once
#include "Canvas.hpp"
#include <memory>
class CanvasEventMemory final {
public:
    static bool apply(const std::shared_ptr<Canvas>& canvas, const nlohmann::json& event);
};
