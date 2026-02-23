#pragma once

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

#include "Device.h"

namespace application {

class DeviceManager {
public:
    void bindSession(const std::string& logicalName, std::uint8_t slaveId, const transport::SessionPtr& session);
    void unbindSessionById(std::uint64_t sessionId);

    std::optional<Device> findByName(const std::string& logicalName) const;
    std::optional<Device> firstConnected() const;

private:
    mutable std::mutex mutex_;
    std::unordered_map<std::string, Device> devices_;
};

} // namespace application
