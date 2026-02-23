#include "application_layer.h"

#include <utility>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

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
            std::lock_guard<std::mutex> lock(transportConfigMutex_);
            transportConfig_.active = false;
        }
    });
}

void ApplicationCore::setJsonResponseCallback(std::function<void(const json::value&)> cb) {
    jsonResponseCallback_ = std::move(cb);
}

bool ApplicationCore::openTcpTransport(const std::string& host, std::uint16_t port, std::string& error) {
    auto session = transportManager_.connectTcpSlave(host, port);
    if (!session) {
        error = "Failed to open TCP transport";
        return false;
    }

    deviceManager_.bindSession("default", 1, session);

    std::lock_guard<std::mutex> lock(transportConfigMutex_);
    transportConfig_.type = transport::ConnectionType::Tcp;
    transportConfig_.host = host;
    transportConfig_.port = port;
    transportConfig_.serialPort.clear();
    transportConfig_.baudRate = 0;
    transportConfig_.stopBits = 0;
    transportConfig_.active = true;
    return true;
}

bool ApplicationCore::openRtuTransport(const std::string& serialPort, std::uint32_t baudRate, std::uint8_t stopBits, std::string& error) {
    auto session = transportManager_.connectSerialSlave(serialPort, baudRate);
    if (!session) {
        error = "Failed to open RTU transport";
        return false;
    }

    deviceManager_.bindSession("default", 1, session);

    std::lock_guard<std::mutex> lock(transportConfigMutex_);
    transportConfig_.type = transport::ConnectionType::Rtu;
    transportConfig_.host.clear();
    transportConfig_.port = 0;
    transportConfig_.serialPort = serialPort;
    transportConfig_.baudRate = baudRate;
    transportConfig_.stopBits = stopBits;
    transportConfig_.active = true;
    return true;
}

bool ApplicationCore::closeActiveTransport(json::object& closedInfo) {
    TransportConfig snapshot;
    {
        std::lock_guard<std::mutex> lock(transportConfigMutex_);
        snapshot = transportConfig_;
    }

    if (!snapshot.active) {
        return false;
    }

    transportManager_.disconnectAll();

    closedInfo["type"] = snapshot.type == transport::ConnectionType::Tcp ? "tcp" : "rtu";
    if (snapshot.type == transport::ConnectionType::Tcp) {
        closedInfo["host"] = snapshot.host;
        closedInfo["port"] = snapshot.port;
    } else {
        closedInfo["serial_port"] = snapshot.serialPort;
        closedInfo["baud_rate"] = snapshot.baudRate;
        closedInfo["stop_bits"] = snapshot.stopBits;
    }

    std::lock_guard<std::mutex> lock(transportConfigMutex_);
    transportConfig_.active = false;
    return true;
}

bool ApplicationCore::switchTransport(const TransportConfig& target, std::string& error, json::object& closedInfo) {
    closeActiveTransport(closedInfo);

    if (target.type == transport::ConnectionType::Tcp) {
        return openTcpTransport(target.host, target.port, error);
    }
    return openRtuTransport(target.serialPort, target.baudRate, target.stopBits, error);
}

TransportConfig ApplicationCore::transportStatus() const {
    std::lock_guard<std::mutex> lock(transportConfigMutex_);
    return transportConfig_;
}

std::vector<std::string> ApplicationCore::listSerialPorts() const {
    std::vector<std::string> ports;
#ifdef _WIN32
    for (int i = 1; i <= 256; ++i) {
        const std::string name = "COM" + std::to_string(i);
        char targetPath[16] = {0};
        if (QueryDosDeviceA(name.c_str(), targetPath, static_cast<DWORD>(sizeof(targetPath))) != 0) {
            ports.push_back(name);
        }
    }
#else
    const std::vector<std::string> prefixes = {"ttyS", "ttyUSB", "ttyACM", "ttyAMA", "rfcomm"};
    const std::filesystem::path devPath{"/dev"};
    if (std::filesystem::exists(devPath)) {
        for (const auto& entry : std::filesystem::directory_iterator(devPath)) {
            const auto fileName = entry.path().filename().string();
            for (const auto& prefix : prefixes) {
                if (fileName.rfind(prefix, 0) == 0) {
                    ports.push_back(entry.path().string());
                    break;
                }
            }
        }
    }
#endif
    return ports;
}

bool ApplicationCore::readRegisters(std::uint8_t slaveId, std::uint16_t address, std::uint16_t count, bool input, std::string& error) {
    protocol::ModbusRequest request;
    request.slaveId = slaveId;
    request.function = input ? protocol::FunctionCode::ReadInputRegisters : protocol::FunctionCode::ReadHoldingRegisters;
    request.startAddress = address;
    request.count = count;
    return sendCommand(request, error);
}

bool ApplicationCore::writeSingleRegister(std::uint8_t slaveId, std::uint16_t address, std::uint16_t value, std::string& error) {
    protocol::ModbusRequest request;
    request.slaveId = slaveId;
    request.function = protocol::FunctionCode::WriteSingleRegister;
    request.startAddress = address;
    request.values = {value};
    return sendCommand(request, error);
}

bool ApplicationCore::writeMultipleRegisters(std::uint8_t slaveId, std::uint16_t address, const std::vector<std::uint16_t>& values, std::string& error) {
    if (values.empty()) {
        error = "Values are empty";
        return false;
    }

    protocol::ModbusRequest request;
    request.slaveId = slaveId;
    request.function = protocol::FunctionCode::WriteMultipleRegisters;
    request.startAddress = address;
    request.count = static_cast<std::uint16_t>(values.size());
    request.values = values;
    return sendCommand(request, error);
}

bool ApplicationCore::readGroup(const std::vector<protocol::ModbusRequest>& requests, std::string& error) {
    for (const auto& request : requests) {
        if (!sendCommand(request, error)) {
            return false;
        }
    }
    return true;
}

bool ApplicationCore::writeGroup(const std::vector<protocol::ModbusRequest>& requests, std::string& error) {
    for (const auto& request : requests) {
        if (!sendCommand(request, error)) {
            return false;
        }
    }
    return true;
}

bool ApplicationCore::sendCommand(const protocol::ModbusRequest& command, std::string& error) {
    auto device = deviceManager_.firstConnected();
    if (!device || !device->session) {
        error = "No active device session";
        return false;
    }

    const auto frame = protocolHandler_.createFrame(command, device->session->connectionType());
    transportManager_.sendToSession(frame, device->session);
    return true;
}

void ApplicationCore::onTransportFrame(const std::vector<std::uint8_t>& frame, const transport::SessionPtr& session) {
    if (!session) {
        return;
    }

    static std::atomic<std::int64_t> requestId{0};
    const auto id = requestId.fetch_add(1);
    const auto responses = protocolHandler_.processIncomingBuffer(frame, session->connectionType(), id);
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
