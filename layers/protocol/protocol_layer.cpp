#include "protocol_layer.h"

#include <cstddef>
#include <stdexcept>

namespace protocol {

bool ProtocolHandler::jsonToRequest(const json::value& payload, ModbusRequest& out, std::string& error) const {
    if (!payload.is_object()) {
        error = "Request must be object";
        return false;
    }

    const auto& obj = payload.as_object();
    if (!obj.contains("slave_id") || !obj.contains("function") || !obj.contains("address")) {
        error = "Missing required fields: slave_id, function, address";
        return false;
    }

    if (!obj.at("slave_id").is_int64()) {
        error = "slave_id must be integer";
        return false;
    }
    const auto slaveId = obj.at("slave_id").as_int64();
    if (slaveId < 0 || slaveId > 255) {
        error = "slave_id out of range";
        return false;
    }
    out.slaveId = static_cast<std::uint8_t>(slaveId);

    if (!obj.at("function").is_string()) {
        error = "function must be string";
        return false;
    }
    FunctionCode fc;
    if (!parseFunction(std::string(obj.at("function").as_string().c_str()), fc)) {
        error = "Unknown function";
        return false;
    }
    out.function = fc;

    if (!obj.at("address").is_int64()) {
        error = "address must be integer";
        return false;
    }
    out.startAddress = static_cast<std::uint16_t>(obj.at("address").as_int64());

    out.count = 1;
    if (obj.contains("count")) {
        if (!obj.at("count").is_int64()) {
            error = "count must be integer";
            return false;
        }
        out.count = static_cast<std::uint16_t>(obj.at("count").as_int64());
    }

    out.values.clear();
    if (obj.contains("values")) {
        if (!obj.at("values").is_array()) {
            error = "values must be array";
            return false;
        }
        for (const auto& v : obj.at("values").as_array()) {
            if (!v.is_int64()) {
                error = "values must contain integers";
                return false;
            }
            out.values.push_back(static_cast<std::uint16_t>(v.as_int64()));
        }
    }

    return true;
}

json::value ProtocolHandler::responseToJson(const ModbusResponse& response, std::int64_t requestId) const {
    json::object root;
    root["jsonrpc"] = "2.0";
    root["id"] = requestId;

    if (response.isException) {
        json::object err;
        err["code"] = -32000;
        err["message"] = "Modbus exception";
        err["data"] = response.exceptionCode;
        root["error"] = err;
        return root;
    }

    json::object result;
    result["slave_id"] = response.slaveId;
    result["function"] = functionToString(response.function);
    json::array values;
    for (const auto value : response.values) {
        values.push_back(value);
    }
    result["values"] = values;
    root["result"] = result;
    return root;
}

std::vector<std::uint8_t> ProtocolHandler::createFrame(
    const ModbusRequest& request,
    transport::ConnectionType connectionType) const {
    auto pdu = createPdu(request);

    if (connectionType == transport::ConnectionType::Rtu) {
        auto frame = pdu;
        const auto crc = crc16(frame);
        frame.push_back(static_cast<std::uint8_t>(crc & 0xFF));
        frame.push_back(static_cast<std::uint8_t>((crc >> 8) & 0xFF));
        return frame;
    }

    std::vector<std::uint8_t> frame;
    frame.reserve(6 + pdu.size());
    frame.push_back(0x00);
    frame.push_back(0x01);
    frame.push_back(0x00);
    frame.push_back(0x00);
    const auto length = static_cast<std::uint16_t>(pdu.size());
    frame.push_back(static_cast<std::uint8_t>((length >> 8) & 0xFF));
    frame.push_back(static_cast<std::uint8_t>(length & 0xFF));
    frame.insert(frame.end(), pdu.begin(), pdu.end());
    return frame;
}

std::vector<json::value> ProtocolHandler::processIncomingBuffer(
    const std::vector<std::uint8_t>& chunk,
    transport::ConnectionType connectionType,
    std::int64_t requestId) {
    std::vector<json::value> result;
    auto& buffer = connectionType == transport::ConnectionType::Tcp ? tcpBuffer_ : rtuBuffer_;
    buffer.insert(buffer.end(), chunk.begin(), chunk.end());

    if (connectionType == transport::ConnectionType::Tcp) {
        while (buffer.size() >= 6) {
            const auto len = static_cast<std::size_t>((buffer[4] << 8) | buffer[5]);
            if (buffer.size() < 6 + len) {
                break;
            }
            std::vector<std::uint8_t> pdu(buffer.begin() + 6, buffer.begin() + 6 + len);
            buffer.erase(buffer.begin(), buffer.begin() + 6 + len);
            result.push_back(responseToJson(parsePdu(pdu), requestId));
        }
        return result;
    }

    std::size_t offset = 0;
    while (buffer.size() >= offset + 5) {
        const std::uint8_t function = buffer[offset + 1];

        std::size_t frameLen = 0;
        if ((function & 0x80U) != 0U) {
            frameLen = 5; // slave + exception function + code + crc(2)
        } else if (function == static_cast<std::uint8_t>(FunctionCode::ReadHoldingRegisters) ||
                   function == static_cast<std::uint8_t>(FunctionCode::ReadInputRegisters)) {
            const std::size_t byteCountIndex = offset + 2;
            if (buffer.size() <= byteCountIndex) {
                break;
            }
            const std::size_t byteCount = buffer[byteCountIndex];
            frameLen = 3 + byteCount + 2; // slave + func + byteCount + data + crc(2)
        } else if (function == static_cast<std::uint8_t>(FunctionCode::WriteSingleRegister) ||
                   function == static_cast<std::uint8_t>(FunctionCode::WriteMultipleRegisters)) {
            frameLen = 8; // slave + func + addr(2) + qty/value(2) + crc(2)
        } else {
            ++offset;
            continue;
        }

        if (buffer.size() < offset + frameLen) {
            break;
        }

        std::vector<std::uint8_t> frame(buffer.begin() + static_cast<std::ptrdiff_t>(offset),
                                        buffer.begin() + static_cast<std::ptrdiff_t>(offset + frameLen));

        const auto expected = static_cast<std::uint16_t>((frame[frameLen - 1] << 8) | frame[frameLen - 2]);
        std::vector<std::uint8_t> pdu(frame.begin(), frame.end() - 2);
        const auto actual = crc16(pdu);
        if (actual != expected) {
            ++offset;
            continue;
        }

        result.push_back(responseToJson(parsePdu(pdu), requestId));
        offset += frameLen;
    }

    if (offset > 0) {
        buffer.erase(buffer.begin(), buffer.begin() + static_cast<std::ptrdiff_t>(offset));
    }

    return result;
}

std::uint16_t ProtocolHandler::crc16(const std::vector<std::uint8_t>& data) {
    std::uint16_t crc = 0xFFFF;
    for (const auto b : data) {
        crc ^= b;
        for (int i = 0; i < 8; ++i) {
            if ((crc & 0x01U) != 0U) {
                crc >>= 1;
                crc ^= 0xA001;
            } else {
                crc >>= 1;
            }
        }
    }
    return crc;
}

std::string ProtocolHandler::functionToString(FunctionCode code) {
    switch (code) {
        case FunctionCode::ReadHoldingRegisters:
            return "read_holding";
        case FunctionCode::ReadInputRegisters:
            return "read_input";
        case FunctionCode::WriteSingleRegister:
            return "write_single";
        case FunctionCode::WriteMultipleRegisters:
            return "write_multiple";
    }
    return "unknown";
}

bool ProtocolHandler::parseFunction(const std::string& name, FunctionCode& code) {
    if (name == "read_holding") {
        code = FunctionCode::ReadHoldingRegisters;
        return true;
    }
    if (name == "read_input") {
        code = FunctionCode::ReadInputRegisters;
        return true;
    }
    if (name == "write_single") {
        code = FunctionCode::WriteSingleRegister;
        return true;
    }
    if (name == "write_multiple") {
        code = FunctionCode::WriteMultipleRegisters;
        return true;
    }
    return false;
}

std::vector<std::uint8_t> ProtocolHandler::createPdu(const ModbusRequest& request) const {
    std::vector<std::uint8_t> pdu;
    pdu.push_back(request.slaveId);
    pdu.push_back(static_cast<std::uint8_t>(request.function));

    pdu.push_back(static_cast<std::uint8_t>((request.startAddress >> 8) & 0xFF));
    pdu.push_back(static_cast<std::uint8_t>(request.startAddress & 0xFF));

    if (request.function == FunctionCode::WriteSingleRegister) {
        const auto value = request.values.empty() ? 0 : request.values.front();
        pdu.push_back(static_cast<std::uint8_t>((value >> 8) & 0xFF));
        pdu.push_back(static_cast<std::uint8_t>(value & 0xFF));
        return pdu;
    }

    pdu.push_back(static_cast<std::uint8_t>((request.count >> 8) & 0xFF));
    pdu.push_back(static_cast<std::uint8_t>(request.count & 0xFF));

    if (request.function == FunctionCode::WriteMultipleRegisters) {
        pdu.push_back(static_cast<std::uint8_t>(request.values.size() * 2));
        for (const auto v : request.values) {
            pdu.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
            pdu.push_back(static_cast<std::uint8_t>(v & 0xFF));
        }
    }

    return pdu;
}

ModbusResponse ProtocolHandler::parsePdu(const std::vector<std::uint8_t>& pdu) const {
    if (pdu.size() < 2) {
        throw std::runtime_error("PDU too short");
    }

    ModbusResponse response;
    response.slaveId = pdu[0];
    response.function = static_cast<FunctionCode>(pdu[1]);

    const auto func = pdu[1];
    if ((func & 0x80U) != 0U) {
        response.isException = true;
        response.exceptionCode = pdu.size() > 2 ? pdu[2] : 0;
        return response;
    }

    if ((func == static_cast<std::uint8_t>(FunctionCode::ReadHoldingRegisters) ||
         func == static_cast<std::uint8_t>(FunctionCode::ReadInputRegisters)) &&
        pdu.size() >= 3) {
        const auto byteCount = pdu[2];
        for (std::size_t i = 0; i + 1 < byteCount && (3 + i + 1) < pdu.size(); i += 2) {
            response.values.push_back(static_cast<std::uint16_t>((pdu[3 + i] << 8) | pdu[3 + i + 1]));
        }
    }

    return response;
}

} // namespace protocol
