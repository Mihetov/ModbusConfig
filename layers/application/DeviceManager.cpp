#include "DeviceManager.h"

namespace application {

void DeviceManager::bindSession(const std::string& logicalName, std::uint8_t slaveId, const transport::SessionPtr& session) {
    std::lock_guard<std::mutex> lock(mutex_);
    devices_[logicalName] = Device{logicalName, slaveId, session};
}

void DeviceManager::unbindSessionById(std::uint64_t sessionId) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& [_, device] : devices_) {
        if (device.session && device.session->id() == sessionId) {
            device.session.reset();
        }
    }
}

std::optional<Device> DeviceManager::findByName(const std::string& logicalName) const {
    std::lock_guard<std::mutex> lock(mutex_);
    auto it = devices_.find(logicalName);
    if (it == devices_.end()) {
        return std::nullopt;
    }
    return it->second;
}

std::optional<Device> DeviceManager::firstConnected() const {
    std::lock_guard<std::mutex> lock(mutex_);
    for (const auto& [_, device] : devices_) {
        if (device.isConnected()) {
            return device;
        }
    }
    return std::nullopt;
}

} // namespace application
