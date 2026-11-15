#pragma once
#include "third-party/json.hpp"
#include <stdexcept>

namespace sharing_policy {
inline bool preloadAllowed(const nlohmann::json& manifest) {
    const auto setting = manifest.find("carryForwardPreload");
    if (setting == manifest.end()) return false;
    if (!setting->is_boolean()) throw std::runtime_error("carryForwardPreload must be a boolean.");
    return setting->get<bool>();
}
}
