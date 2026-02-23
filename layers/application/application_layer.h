#pragma once

#include <boost/json.hpp>

#include <functional>
#include <string>

#include "DeviceManager.h"
#include "layers/protocol/protocol_layer.h"
#include "layers/transport/transport_layer.h"

namespace application {

class TaskScheduler {
public:
    void post(const std::function<void()>& task) const { task(); }
};

class ApplicationCore {
public:
    explicit ApplicationCore(transport::TransportManager& transportManager);

    void setJsonResponseCallback(std::function<void(const boost::json::value&)> cb);
    void handleJsonRequest(const boost::json::value& request);

    DeviceManager& deviceManager() noexcept { return deviceManager_; }

private:
    void onTransportFrame(const std::vector<std::uint8_t>& frame, const transport::SessionPtr& session);
    void emitJson(const boost::json::value& value) const;

    transport::TransportManager& transportManager_;
    protocol::ProtocolHandler protocolHandler_;
    DeviceManager deviceManager_;
    TaskScheduler taskScheduler_;
    std::function<void(const boost::json::value&)> jsonResponseCallback_;
};

} // namespace application
