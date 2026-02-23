#pragma once

#include <boost/json.hpp>

#include <cstdint>
#include <string>
#include <vector>

#include "layers/transport/transport_layer.h"

namespace protocol {

namespace json = boost::json;

enum class FunctionCode : std::uint8_t {
    ReadHoldingRegisters = 0x03,
    ReadInputRegisters = 0x04,
    WriteSingleRegister = 0x06,
    WriteMultipleRegisters = 0x10
};

struct ModbusRequest {
    std::uint8_t slaveId = 0;
    FunctionCode function = FunctionCode::ReadHoldingRegisters;
    std::uint16_t startAddress = 0;
    std::uint16_t count = 1;
    std::vector<std::uint16_t> values;
};

struct ModbusResponse {
    std::uint8_t slaveId = 0;
    FunctionCode function = FunctionCode::ReadHoldingRegisters;
    std::vector<std::uint16_t> values;
    bool isException = false;
    std::uint8_t exceptionCode = 0;
};

class ProtocolHandler {
public:
    bool jsonToRequest(const json::value& payload, ModbusRequest& out, std::string& error) const;
    json::value responseToJson(const ModbusResponse& response, std::int64_t requestId) const;

    std::vector<std::uint8_t> createFrame(const ModbusRequest& request, transport::ConnectionType connectionType) const;
    std::vector<json::value> processIncomingBuffer(
        const std::vector<std::uint8_t>& chunk,
        transport::ConnectionType connectionType,
        std::int64_t requestId);

private:
    static std::uint16_t crc16(const std::vector<std::uint8_t>& data);
    static std::string functionToString(FunctionCode code);
    static bool parseFunction(const std::string& name, FunctionCode& code);

    std::vector<std::uint8_t> createPdu(const ModbusRequest& request) const;
    ModbusResponse parsePdu(const std::vector<std::uint8_t>& pdu) const;

    std::vector<std::uint8_t> tcpBuffer_;
    std::vector<std::uint8_t> rtuBuffer_;
};

} // namespace protocol
