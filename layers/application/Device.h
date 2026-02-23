#pragma once

#include <cstdint>
#include <optional>
#include <string>

#include "layers/transport/transport_layer.h"

namespace application {

struct Device {
    std::string logicalName;
    std::uint8_t slaveId = 1;
    transport::SessionPtr session;

    bool isConnected() const noexcept { return static_cast<bool>(session); }
};

} // namespace application
