#pragma once
#include "Canvas.hpp"
#include <memory>
class CanvasPersistenceService final {
public:
    static void drain(const std::shared_ptr<Canvas>& canvas);
};
