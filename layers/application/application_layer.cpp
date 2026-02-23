#include "application_layer.h"

namespace application {

namespace json = boost::json;

ApplicationCore::ApplicationCore(transport::TransportManager& transportManager)
    : transportManager_(transportManager) {
    transportManager_.setFrameCallback(
        [this](const std::vector<std::uint8_t>& frame, const transport::SessionPtr& session) {
            onTransportFrame(frame, session);
        });

    transportManager_.setConnectionCallback([this](bool connected, const transport::SessionPtr& session) {
        if (!connected && session) {
            deviceManager_.unbindSessionById(session->id());
        }
    });
}

void ApplicationCore::setJsonResponseCallback(std::function<void(const json::value&)> cb) {
    jsonResponseCallback_ = std::move(cb);
}

void ApplicationCore::handleJsonRequest(const json::value& request) {
    taskScheduler_.post([this, request]() {
        protocol::ModbusRequest command;
        std::string error;

        if (!protocolHandler_.jsonToRequest(request, command, error)) {
            json::object response;
            response["jsonrpc"] = "2.0";
            response["id"] = request.is_object() && request.as_object().contains("id")
                                 ? request.as_object().at("id")
                                 : json::value(nullptr);
            json::object err;
            err["code"] = -32600;
            err["message"] = error;
            response["error"] = err;
            emitJson(response);
            return;
        }

        auto device = deviceManager_.firstConnected();
        if (!device || !device->session) {
            json::object response;
            response["jsonrpc"] = "2.0";
            response["id"] = request.is_object() && request.as_object().contains("id")
                                 ? request.as_object().at("id")
                                 : json::value(nullptr);
            json::object err;
            err["code"] = -32603;
            err["message"] = "No active device session";
            response["error"] = err;
            emitJson(response);
            return;
        }

        const auto frame = protocolHandler_.createFrame(command, device->session->connectionType());
        transportManager_.sendToSession(frame, device->session);
    });
}

void ApplicationCore::onTransportFrame(const std::vector<std::uint8_t>& frame, const transport::SessionPtr& session) {
    if (!session) {
        return;
    }

    constexpr std::int64_t requestId = 0;
    const auto responses = protocolHandler_.processIncomingBuffer(frame, session->connectionType(), requestId);
    for (const auto& response : responses) {
        emitJson(response);
    }
}

void ApplicationCore::emitJson(const json::value& value) const {
    if (jsonResponseCallback_) {
        jsonResponseCallback_(value);
    }
}

} // namespace application
