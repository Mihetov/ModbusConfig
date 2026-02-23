#pragma once

#include <boost/json.hpp>

#include <atomic>
#include <condition_variable>
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

#include "DeviceManager.h"
#include "layers/protocol/protocol_layer.h"
#include "layers/transport/transport_layer.h"

namespace application {

class TaskScheduler {
public:
    void post(const std::function<void()>& task) const { task(); }
};

struct TransportConfig {
    transport::ConnectionType type = transport::ConnectionType::Tcp;
    std::string host;
    std::uint16_t port = 0;
    std::string serialPort;
    std::uint32_t baudRate = 9600;
    std::uint8_t stopBits = 1;
    bool active = false;
};

class ApplicationCore {
public:
    explicit ApplicationCore(transport::TransportManager& transportManager);

    void setJsonResponseCallback(std::function<void(const boost::json::value&)> cb);

    bool openTcpTransport(const std::string& host, std::uint16_t port, std::string& error);
    bool openRtuTransport(const std::string& serialPort, std::uint32_t baudRate, std::uint8_t stopBits, std::string& error);
    bool closeActiveTransport(boost::json::object& closedInfo);
    bool switchTransport(const TransportConfig& target, std::string& error, boost::json::object& closedInfo);
    TransportConfig transportStatus() const;
    std::vector<std::string> listSerialPorts() const;

    bool readRegisters(std::uint8_t slaveId, std::uint16_t address, std::uint16_t count, bool input, std::string& error);
    bool readRegistersDetailed(std::uint8_t slaveId, std::uint16_t address, std::uint16_t count, bool input,
                              boost::json::object& result, std::string& error, std::uint32_t timeoutMs = 2000);
    bool writeSingleRegister(std::uint8_t slaveId, std::uint16_t address, std::uint16_t value, std::string& error);
    bool writeMultipleRegisters(std::uint8_t slaveId, std::uint16_t address, const std::vector<std::uint16_t>& values, std::string& error);
    bool readGroup(const std::vector<protocol::ModbusRequest>& requests, std::string& error);
    bool readGroupDetailed(const std::vector<protocol::ModbusRequest>& requests, boost::json::array& results,
                          std::string& error, std::uint32_t timeoutMs = 2000);
    bool writeGroup(const std::vector<protocol::ModbusRequest>& requests, std::string& error);

    DeviceManager& deviceManager() noexcept { return deviceManager_; }

private:
    struct PendingReadContext {
        std::uint64_t token = 0;
        std::uint8_t slaveId = 0;
        std::uint16_t address = 0;
        std::uint16_t count = 0;
    };

    bool sendCommand(const protocol::ModbusRequest& command, std::string& error);
    bool sendReadAndWait(const protocol::ModbusRequest& command, boost::json::object& result, std::string& error, std::uint32_t timeoutMs);

    void onTransportFrame(const std::vector<std::uint8_t>& frame, const transport::SessionPtr& session);
    void handleReadResponse(const boost::json::object& responseObject);
    void emitJson(const boost::json::value& value) const;

    transport::TransportManager& transportManager_;
    protocol::ProtocolHandler protocolHandler_;
    DeviceManager deviceManager_;
    TaskScheduler taskScheduler_;
    std::function<void(const boost::json::value&)> jsonResponseCallback_;

    mutable std::mutex transportConfigMutex_;
    TransportConfig transportConfig_;

    std::atomic<std::uint64_t> nextReadToken_{1};
    std::mutex pendingReadsMutex_;
    std::condition_variable pendingReadsCv_;
    std::deque<PendingReadContext> pendingReads_;
    std::unordered_map<std::uint64_t, boost::json::object> completedReads_;
};

} // namespace application
