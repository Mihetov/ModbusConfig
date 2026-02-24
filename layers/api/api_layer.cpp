#include "api_layer.h"

#include <boost/beast/version.hpp>

#include <algorithm>
#include <charconv>
#include <cctype>
#include <cstring>
#include <sstream>
#include <limits>

namespace api {

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = boost::asio::ip::tcp;

namespace {

bool parseUint16Flexible(const json::value& value, std::uint16_t& out) {
    if (value.is_int64()) {
        const auto v = value.as_int64();
        if (v < 0 || v > 0xFFFF) {
            return false;
        }
        out = static_cast<std::uint16_t>(v);
        return true;
    }

    if (!value.is_string()) {
        return false;
    }

    std::string text = std::string(value.as_string().c_str());
    int base = 10;
    if (text.rfind("0x", 0) == 0 || text.rfind("0X", 0) == 0) {
        text = text.substr(2);
        base = 16;
    }

    unsigned int parsed = 0;
    const auto* begin = text.data();
    const auto* end = begin + text.size();
    const auto res = std::from_chars(begin, end, parsed, base);
    if (res.ec != std::errc() || res.ptr != end || parsed > 0xFFFF) {
        return false;
    }

    out = static_cast<std::uint16_t>(parsed);
    return true;
}

bool parseUint8Strict(const json::object& obj, const char* key, std::uint8_t& out) {
    if (!obj.contains(key) || !obj.at(key).is_int64()) {
        return false;
    }
    const auto v = obj.at(key).as_int64();
    if (v < 0 || v > 255) {
        return false;
    }
    out = static_cast<std::uint8_t>(v);
    return true;
}

bool parseAddressField(const json::object& obj, std::uint16_t& out) {
    if (!obj.contains("address")) {
        return false;
    }
    return parseUint16Flexible(obj.at("address"), out);
}

std::string toLowerAscii(const std::string& src) {
    std::string out = src;
    std::transform(out.begin(), out.end(), out.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return out;
}

std::string canonicalDataType(const std::string& value) {
    const auto normalized = toLowerAscii(value);
    if (normalized == "word") {
        return "Word";
    }
    if (normalized == "byte") {
        return "Byte";
    }
    if (normalized == "int8") {
        return "Int8";
    }
    if (normalized == "int16") {
        return "Int16";
    }
    if (normalized == "int32") {
        return "Int32";
    }
    if (normalized == "float") {
        return "Float";
    }
    if (normalized == "string") {
        return "String";
    }
    if (normalized == "array") {
        return "Array";
    }
    if (normalized == "tcp56") {
        return "TCP56";
    }
    return {};
}

std::vector<std::uint8_t> registersToBytes(const json::array& values) {
    std::vector<std::uint8_t> bytes;
    bytes.reserve(values.size() * 2);
    for (const auto& value : values) {
        if (!value.is_int64()) {
            continue;
        }
        const auto reg = static_cast<std::uint16_t>(value.as_int64());
        bytes.push_back(static_cast<std::uint8_t>((reg >> 8) & 0xFF));
        bytes.push_back(static_cast<std::uint8_t>(reg & 0xFF));
    }
    return bytes;
}

json::value decodeRegisters(const json::array& values, const std::string& dataType, const json::object& params,
                            std::string& error) {
    if (dataType == "Word") {
        return values;
    }

    const auto bytes = registersToBytes(values);
    if (dataType == "Byte") {
        json::array out;
        for (const auto b : bytes) {
            out.push_back(b);
        }
        return out;
    }

    if (dataType == "Int8") {
        json::array out;
        for (const auto b : bytes) {
            out.push_back(static_cast<std::int8_t>(b));
        }
        return out;
    }

    if (dataType == "Int16") {
        json::array out;
        for (const auto& v : values) {
            out.push_back(static_cast<std::int16_t>(v.as_int64()));
        }
        return out;
    }

    if (dataType == "Int32") {
        if (bytes.size() < 4) {
            error = "Int32 requires at least 2 registers";
            return nullptr;
        }
        const std::int32_t value = (static_cast<std::int32_t>(bytes[0]) << 24) |
                                   (static_cast<std::int32_t>(bytes[1]) << 16) |
                                   (static_cast<std::int32_t>(bytes[2]) << 8) |
                                   static_cast<std::int32_t>(bytes[3]);
        return value;
    }

    if (dataType == "Float") {
        if (bytes.size() < 4) {
            error = "Float requires at least 2 registers";
            return nullptr;
        }
        const std::uint32_t raw = (static_cast<std::uint32_t>(bytes[0]) << 24) |
                                  (static_cast<std::uint32_t>(bytes[1]) << 16) |
                                  (static_cast<std::uint32_t>(bytes[2]) << 8) |
                                  static_cast<std::uint32_t>(bytes[3]);
        float value = 0.0F;
        std::memcpy(&value, &raw, sizeof(value));
        return value;
    }

    if (dataType == "String") {
        std::size_t length = bytes.size();
        if (params.contains("string_length") && params.at("string_length").is_int64() && params.at("string_length").as_int64() > 0) {
            length = static_cast<std::size_t>(params.at("string_length").as_int64());
        }
        length = std::min(length, bytes.size());
        std::string out(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(length));
        const auto zeroPos = out.find('\0');
        if (zeroPos != std::string::npos) {
            out.resize(zeroPos);
        }
        return json::value(out);
    }

    if (dataType == "Array") {
        json::array out;
        for (const auto& value : values) {
            out.push_back(value);
        }
        return out;
    }

    if (dataType == "TCP56") {
        if (bytes.size() < 7) {
            error = "TCP56 requires at least 4 registers";
            return nullptr;
        }
        const std::uint16_t millis = static_cast<std::uint16_t>(bytes[0] | (bytes[1] << 8));
        const std::uint8_t minute = bytes[2] & 0x3F;
        const std::uint8_t hour = bytes[3] & 0x1F;
        const std::uint8_t day = bytes[4] & 0x1F;
        const std::uint8_t month = bytes[5] & 0x0F;
        const int year = 2000 + (bytes[6] & 0x7F);

        json::object out;
        out["milliseconds"] = millis;
        out["minute"] = minute;
        out["hour"] = hour;
        out["day"] = day;
        out["month"] = month;
        out["year"] = year;

        std::ostringstream iso;
        iso << year << "-";
        if (month < 10) {
            iso << "0";
        }
        iso << static_cast<int>(month) << "-";
        if (day < 10) {
            iso << "0";
        }
        iso << static_cast<int>(day) << "T";
        if (hour < 10) {
            iso << "0";
        }
        iso << static_cast<int>(hour) << ":";
        if (minute < 10) {
            iso << "0";
        }
        iso << static_cast<int>(minute) << ":";
        const auto seconds = millis / 1000;
        const auto ms = millis % 1000;
        if (seconds < 10) {
            iso << "0";
        }
        iso << seconds << ".";
        if (ms < 100) {
            iso << "0";
        }
        if (ms < 10) {
            iso << "0";
        }
        iso << ms;
        out["iso8601"] = iso.str();
        return out;
    }

    error = "Unsupported data_type";
    return nullptr;
}

bool enrichReadResultWithType(json::object& readResult, const json::object& params, std::string& error) {
    const std::string requested = params.contains("data_type") && params.at("data_type").is_string()
                                      ? std::string(params.at("data_type").as_string().c_str())
                                      : "Word";
    const auto canonical = canonicalDataType(requested);
    if (canonical.empty()) {
        error = "Unsupported data_type";
        return false;
    }

    if (!readResult.contains("values") || !readResult.at("values").is_array()) {
        error = "Internal error: values field is missing";
        return false;
    }

    const auto decoded = decodeRegisters(readResult.at("values").as_array(), canonical, params, error);
    if (decoded.is_null()) {
        return false;
    }

    readResult["data_type"] = canonical;
    readResult["decoded"] = decoded;
    return true;
}


bool encodeWritePayload(const json::object& params, std::string& canonicalType, std::vector<std::uint16_t>& outRegisters,
                        json::value& sourceValue, std::string& error) {
    const std::string requested = params.contains("data_type") && params.at("data_type").is_string()
                                      ? std::string(params.at("data_type").as_string().c_str())
                                      : "Word";
    canonicalType = canonicalDataType(requested);
    if (canonicalType.empty()) {
        error = "Unsupported data_type";
        return false;
    }

    sourceValue = nullptr;
    if (params.contains("values")) {
        sourceValue = params.at("values");
    } else if (params.contains("value")) {
        sourceValue = params.at("value");
    }

    outRegisters.clear();
    auto pushReg = [&](std::int64_t raw) -> bool {
        if (raw < 0 || raw > 0xFFFF) {
            error = "Register value out of range [0..65535]";
            return false;
        }
        outRegisters.push_back(static_cast<std::uint16_t>(raw));
        return true;
    };

    if (canonicalType == "Word" || canonicalType == "Array") {
        if (!params.contains("values") || !params.at("values").is_array()) {
            if (!params.contains("value") || !params.at("value").is_int64()) {
                error = "value or values required";
                return false;
            }
            return pushReg(params.at("value").as_int64());
        }
        for (const auto& v : params.at("values").as_array()) {
            if (!v.is_int64()) {
                error = "values must be int array";
                return false;
            }
            if (!pushReg(v.as_int64())) {
                return false;
            }
        }
        return !outRegisters.empty();
    }

    if (canonicalType == "Int16") {
        if (params.contains("values") && params.at("values").is_array()) {
            for (const auto& v : params.at("values").as_array()) {
                if (!v.is_int64()) {
                    error = "values must be int array";
                    return false;
                }
                const auto n = v.as_int64();
                if (n < -32768 || n > 32767) {
                    error = "Int16 value out of range";
                    return false;
                }
                outRegisters.push_back(static_cast<std::uint16_t>(static_cast<std::int16_t>(n)));
            }
            return !outRegisters.empty();
        }
        if (!params.contains("value") || !params.at("value").is_int64()) {
            error = "value or values required";
            return false;
        }
        const auto n = params.at("value").as_int64();
        if (n < -32768 || n > 32767) {
            error = "Int16 value out of range";
            return false;
        }
        outRegisters.push_back(static_cast<std::uint16_t>(static_cast<std::int16_t>(n)));
        return true;
    }

    if (canonicalType == "Int32") {
        if (!params.contains("value") || !params.at("value").is_int64()) {
            error = "Int32 requires integer value";
            return false;
        }
        const auto n = params.at("value").as_int64();
        if (n < std::numeric_limits<std::int32_t>::min() || n > std::numeric_limits<std::int32_t>::max()) {
            error = "Int32 value out of range";
            return false;
        }
        const auto raw = static_cast<std::uint32_t>(static_cast<std::int32_t>(n));
        outRegisters.push_back(static_cast<std::uint16_t>((raw >> 16) & 0xFFFF));
        outRegisters.push_back(static_cast<std::uint16_t>(raw & 0xFFFF));
        return true;
    }

    if (canonicalType == "Float") {
        if (!params.contains("value") || (!params.at("value").is_double() && !params.at("value").is_int64())) {
            error = "Float requires numeric value";
            return false;
        }
        const float f = params.at("value").is_double() ? static_cast<float>(params.at("value").as_double())
                                                         : static_cast<float>(params.at("value").as_int64());
        std::uint32_t raw = 0;
        std::memcpy(&raw, &f, sizeof(raw));
        outRegisters.push_back(static_cast<std::uint16_t>((raw >> 16) & 0xFFFF));
        outRegisters.push_back(static_cast<std::uint16_t>(raw & 0xFFFF));
        return true;
    }

    if (canonicalType == "String") {
        if (!params.contains("value") || !params.at("value").is_string()) {
            error = "String requires string value";
            return false;
        }
        std::string text = std::string(params.at("value").as_string().c_str());
        if (params.contains("string_length") && params.at("string_length").is_int64() && params.at("string_length").as_int64() > 0) {
            const auto maxLen = static_cast<std::size_t>(params.at("string_length").as_int64());
            if (text.size() < maxLen) {
                text.resize(maxLen, '\0');
            } else if (text.size() > maxLen) {
                text.resize(maxLen);
            }
        }
        if ((text.size() % 2U) != 0U) {
            text.push_back('\0');
        }
        for (std::size_t i = 0; i < text.size(); i += 2) {
            const auto hi = static_cast<std::uint8_t>(text[i]);
            const auto lo = static_cast<std::uint8_t>(text[i + 1]);
            outRegisters.push_back(static_cast<std::uint16_t>((hi << 8) | lo));
        }
        return true;
    }

    if (canonicalType == "Byte" || canonicalType == "Int8") {
        std::vector<std::uint8_t> bytes;
        if (params.contains("values") && params.at("values").is_array()) {
            for (const auto& v : params.at("values").as_array()) {
                if (!v.is_int64()) {
                    error = "values must be int array";
                    return false;
                }
                const auto n = v.as_int64();
                if (canonicalType == "Int8") {
                    if (n < -128 || n > 127) {
                        error = "Int8 value out of range";
                        return false;
                    }
                    bytes.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(n)));
                } else {
                    if (n < 0 || n > 255) {
                        error = "Byte value out of range";
                        return false;
                    }
                    bytes.push_back(static_cast<std::uint8_t>(n));
                }
            }
        } else if (params.contains("value") && params.at("value").is_int64()) {
            const auto n = params.at("value").as_int64();
            if (canonicalType == "Int8") {
                if (n < -128 || n > 127) {
                    error = "Int8 value out of range";
                    return false;
                }
                bytes.push_back(static_cast<std::uint8_t>(static_cast<std::int8_t>(n)));
            } else {
                if (n < 0 || n > 255) {
                    error = "Byte value out of range";
                    return false;
                }
                bytes.push_back(static_cast<std::uint8_t>(n));
            }
        } else {
            error = "value or values required";
            return false;
        }

        if ((bytes.size() % 2U) != 0U) {
            bytes.push_back(0U);
        }
        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            outRegisters.push_back(static_cast<std::uint16_t>((bytes[i] << 8) | bytes[i + 1]));
        }
        return true;
    }

    if (canonicalType == "TCP56") {
        if (!params.contains("value") || !params.at("value").is_object()) {
            error = "TCP56 requires object value";
            return false;
        }
        const auto& obj = params.at("value").as_object();
        const char* required[] = {"milliseconds", "minute", "hour", "day", "month", "year"};
        for (const auto* key : required) {
            if (!obj.contains(key) || !obj.at(key).is_int64()) {
                error = std::string("TCP56 value requires integer field: ") + key;
                return false;
            }
        }
        const auto millis = obj.at("milliseconds").as_int64();
        const auto minute = obj.at("minute").as_int64();
        const auto hour = obj.at("hour").as_int64();
        const auto day = obj.at("day").as_int64();
        const auto month = obj.at("month").as_int64();
        const auto year = obj.at("year").as_int64();
        if (millis < 0 || millis > 59999 || minute < 0 || minute > 59 || hour < 0 || hour > 23 || day < 1 || day > 31 ||
            month < 1 || month > 12 || year < 2000 || year > 2127) {
            error = "TCP56 fields out of range";
            return false;
        }

        std::vector<std::uint8_t> bytes(8, 0U);
        bytes[0] = static_cast<std::uint8_t>(millis & 0xFF);
        bytes[1] = static_cast<std::uint8_t>((millis >> 8) & 0xFF);
        bytes[2] = static_cast<std::uint8_t>(minute & 0x3F);
        bytes[3] = static_cast<std::uint8_t>(hour & 0x1F);
        bytes[4] = static_cast<std::uint8_t>(day & 0x1F);
        bytes[5] = static_cast<std::uint8_t>(month & 0x0F);
        bytes[6] = static_cast<std::uint8_t>((year - 2000) & 0x7F);

        for (std::size_t i = 0; i < bytes.size(); i += 2) {
            outRegisters.push_back(static_cast<std::uint16_t>((bytes[i] << 8) | bytes[i + 1]));
        }
        return true;
    }

    error = "Unsupported data_type";
    return false;
}

} // namespace

ApiController::ApiController(application::ApplicationCore& appCore)
    : appCore_(appCore) {}

json::value ApiController::processRequest(const json::value& request) {
    if (request.is_array()) {
        return processBatch(request.as_array());
    }

    if (!request.is_object()) {
        return errorResponse(nullptr, -32600, "Invalid JSON-RPC payload");
    }

    return processSingle(request.as_object());
}

json::array ApiController::processBatch(const json::array& requests) {
    json::array responses;
    for (const auto& item : requests) {
        if (!item.is_object()) {
            responses.emplace_back(errorResponse(nullptr, -32600, "Batch item must be object"));
            continue;
        }
        responses.emplace_back(processSingle(item.as_object()));
    }
    return responses;
}

json::value ApiController::processSingle(const json::object& req) {
    const json::value id = req.contains("id") ? req.at("id") : json::value(nullptr);

    if (!req.contains("method") || !req.at("method").is_string()) {
        return errorResponse(id, -32600, "Missing method");
    }

    const std::string method = req.at("method").as_string().c_str();
    const json::object params = req.contains("params") && req.at("params").is_object()
                                    ? req.at("params").as_object()
                                    : json::object{};

    if (method == "ping") {
        json::object result;
        result["status"] = "ok";
        result["service"] = "modbus-host";
        return okResponse(id, result);
    }

    if (method == "transport.serial_ports") {
        json::array ports;
        for (const auto& p : appCore_.listSerialPorts()) {
            ports.emplace_back(p);
        }
        return okResponse(id, json::object{{"ports", ports}});
    }

    if (method == "transport.status") {
        const auto status = appCore_.transportStatus();
        json::object result;
        result["active"] = status.active;
        result["type"] = status.type == transport::ConnectionType::Tcp ? "tcp" : "rtu";
        result["host"] = status.host;
        result["port"] = status.port;
        result["serial_port"] = status.serialPort;
        result["baud_rate"] = status.baudRate;
        result["stop_bits"] = status.stopBits;
        return okResponse(id, result);
    }

    if (method == "transport.close") {
        json::object closed;
        const auto closedOk = appCore_.closeActiveTransport(closed);
        json::object result;
        result["closed"] = closedOk;
        result["details"] = closed;
        return okResponse(id, result);
    }

    if (method == "transport.open" || method == "transport.switch") {
        if (!params.contains("type") || !params.at("type").is_string()) {
            return errorResponse(id, -32602, "type is required");
        }

        application::TransportConfig cfg;
        const std::string type = params.at("type").as_string().c_str();
        if (type == "tcp") {
            cfg.type = transport::ConnectionType::Tcp;
            if (!params.contains("host") || !params.contains("port")) {
                return errorResponse(id, -32602, "host and port are required for tcp");
            }
            cfg.host = std::string(params.at("host").as_string().c_str());
            cfg.port = static_cast<std::uint16_t>(params.at("port").as_int64());
        } else if (type == "rtu") {
            cfg.type = transport::ConnectionType::Rtu;
            if (!params.contains("serial_port") || !params.contains("baud_rate")) {
                return errorResponse(id, -32602, "serial_port and baud_rate are required for rtu");
            }
            cfg.serialPort = std::string(params.at("serial_port").as_string().c_str());
            cfg.baudRate = static_cast<std::uint32_t>(params.at("baud_rate").as_int64());
            cfg.stopBits = params.contains("stop_bits") ? static_cast<std::uint8_t>(params.at("stop_bits").as_int64()) : 1;
        } else {
            return errorResponse(id, -32602, "Unknown transport type");
        }

        std::string error;
        bool ok = false;
        json::object closed;

        if (method == "transport.switch") {
            ok = appCore_.switchTransport(cfg, error, closed);
        } else {
            if (cfg.type == transport::ConnectionType::Tcp) {
                ok = appCore_.openTcpTransport(cfg.host, cfg.port, error);
            } else {
                ok = appCore_.openRtuTransport(cfg.serialPort, cfg.baudRate, cfg.stopBits, error);
            }
        }

        if (!ok) {
            return errorResponse(id, -32001, error.empty() ? "Failed to open transport" : error);
        }

        json::object result;
        result["opened"] = true;
        result["type"] = type;
        result["closed_previous"] = closed;
        return okResponse(id, result);
    }

    if (method == "modbus.read") {
        if (!params.contains("slave_id") || !params.contains("address") || !params.contains("count")) {
            return errorResponse(id, -32602, "slave_id, address, count are required");
        }

        std::uint8_t slaveId = 0;
        std::uint16_t address = 0;
        if (!parseUint8Strict(params, "slave_id", slaveId) || !parseAddressField(params, address) || !params.at("count").is_int64()) {
            return errorResponse(id, -32602, "Invalid slave_id/address/count format");
        }

        std::string error;
        json::object readResult;
        const bool input = params.contains("input") && params.at("input").as_bool();
        const std::uint32_t timeoutMs = params.contains("timeout_ms") && params.at("timeout_ms").is_int64()
                                            ? static_cast<std::uint32_t>(params.at("timeout_ms").as_int64())
                                            : 2000U;
        const bool ok = appCore_.readRegistersDetailed(
            slaveId,
            address,
            static_cast<std::uint16_t>(params.at("count").as_int64()),
            input,
            readResult,
            error,
            timeoutMs);
        if (!ok) {
            return errorResponse(id, -32002, error);
        }

        if (!enrichReadResultWithType(readResult, params, error)) {
            return errorResponse(id, -32602, error);
        }
        return okResponse(id, readResult);
    }

    if (method == "modbus.read_group") {
        if (!params.contains("requests") || !params.at("requests").is_array()) {
            return errorResponse(id, -32602, "requests array is required");
        }

        std::vector<protocol::ModbusRequest> requests;
        for (const auto& item : params.at("requests").as_array()) {
            if (!item.is_object()) {
                return errorResponse(id, -32602, "requests[] item must be object");
            }
            const auto& r = item.as_object();
            protocol::ModbusRequest req;
            if (!parseUint8Strict(r, "slave_id", req.slaveId) || !parseAddressField(r, req.startAddress) ||
                !r.contains("count") || !r.at("count").is_int64()) {
                return errorResponse(id, -32602, "Invalid group read item format");
            }
            req.count = static_cast<std::uint16_t>(r.at("count").as_int64());
            req.function = r.contains("input") && r.at("input").as_bool()
                               ? protocol::FunctionCode::ReadInputRegisters
                               : protocol::FunctionCode::ReadHoldingRegisters;
            requests.push_back(req);
        }

        std::string error;
        json::array groupResults;
        const std::uint32_t timeoutMs = params.contains("timeout_ms") && params.at("timeout_ms").is_int64()
                                            ? static_cast<std::uint32_t>(params.at("timeout_ms").as_int64())
                                            : 2000U;
        if (!appCore_.readGroupDetailed(requests, groupResults, error, timeoutMs)) {
            return errorResponse(id, -32002, error);
        }

        const auto& requestItems = params.at("requests").as_array();
        for (std::size_t i = 0; i < groupResults.size() && i < requestItems.size(); ++i) {
            if (!groupResults[i].is_object()) {
                continue;
            }
            const json::object reqParams = requestItems[i].as_object();
            auto resultObj = groupResults[i].as_object();
            if (!enrichReadResultWithType(resultObj, reqParams, error)) {
                return errorResponse(id, -32602, "requests[" + std::to_string(i) + "]: " + error);
            }
            groupResults[i] = resultObj;
        }

        json::object payload;
        payload["ok"] = true;
        payload["count"] = requests.size();
        payload["results"] = groupResults;
        return okResponse(id, payload);
    }

    if (method == "modbus.write") {
        if (!params.contains("slave_id") || !params.contains("address")) {
            return errorResponse(id, -32602, "slave_id and address are required");
        }

        std::uint8_t slaveId = 0;
        std::uint16_t address = 0;
        if (!parseUint8Strict(params, "slave_id", slaveId) || !parseAddressField(params, address)) {
            return errorResponse(id, -32602, "Invalid slave_id/address format");
        }

        std::string error;
        std::string dataType;
        std::vector<std::uint16_t> encodedRegisters;
        json::value sourceValue;
        if (!encodeWritePayload(params, dataType, encodedRegisters, sourceValue, error)) {
            return errorResponse(id, -32602, error);
        }
        if (encodedRegisters.empty()) {
            return errorResponse(id, -32602, "No data to write");
        }

        const bool ok = encodedRegisters.size() == 1
                            ? appCore_.writeSingleRegister(slaveId, address, encodedRegisters.front(), error)
                            : appCore_.writeMultipleRegisters(slaveId, address, encodedRegisters, error);

        if (!ok) {
            return errorResponse(id, -32003, error);
        }

        json::array registersJson;
        for (const auto reg : encodedRegisters) {
            registersJson.push_back(reg);
        }
        json::object result;
        result["accepted"] = true;
        result["slave_id"] = slaveId;
        result["address"] = address;
        result["data_type"] = dataType;
        result["input"] = sourceValue;
        result["written_registers"] = registersJson;
        result["register_count"] = encodedRegisters.size();
        return okResponse(id, result);
    }

    if (method == "modbus.write_group") {
        if (!params.contains("requests") || !params.at("requests").is_array()) {
            return errorResponse(id, -32602, "requests array is required");
        }

        std::vector<protocol::ModbusRequest> requests;
        json::array results;
        std::size_t idx = 0;
        for (const auto& item : params.at("requests").as_array()) {
            if (!item.is_object()) {
                return errorResponse(id, -32602, "requests[] item must be object");
            }
            const auto& r = item.as_object();
            protocol::ModbusRequest req;
            if (!parseUint8Strict(r, "slave_id", req.slaveId) || !parseAddressField(r, req.startAddress)) {
                return errorResponse(id, -32602, "Invalid group write item format");
            }

            std::string error;
            std::string dataType;
            std::vector<std::uint16_t> encodedRegisters;
            json::value sourceValue;
            if (!encodeWritePayload(r, dataType, encodedRegisters, sourceValue, error)) {
                return errorResponse(id, -32602, "requests[" + std::to_string(idx) + "]: " + error);
            }
            if (encodedRegisters.empty()) {
                return errorResponse(id, -32602, "requests[" + std::to_string(idx) + "]: No data to write");
            }

            req.function = encodedRegisters.size() == 1 ? protocol::FunctionCode::WriteSingleRegister
                                                        : protocol::FunctionCode::WriteMultipleRegisters;
            req.values = encodedRegisters;
            req.count = static_cast<std::uint16_t>(encodedRegisters.size());
            requests.push_back(req);

            json::array encoded;
            for (const auto reg : encodedRegisters) {
                encoded.push_back(reg);
            }
            json::object itemResult;
            itemResult["index"] = idx;
            itemResult["slave_id"] = req.slaveId;
            itemResult["address"] = req.startAddress;
            itemResult["data_type"] = dataType;
            itemResult["input"] = sourceValue;
            itemResult["written_registers"] = encoded;
            itemResult["register_count"] = encodedRegisters.size();
            results.push_back(itemResult);
            ++idx;
        }

        std::string error;
        if (!appCore_.writeGroup(requests, error)) {
            return errorResponse(id, -32003, error);
        }
        return okResponse(id, json::object{{"accepted", true}, {"count", requests.size()}, {"results", results}});
    }

    return errorResponse(id, -32601, "Method not found");
}

json::value ApiController::errorResponse(const json::value& id, int code, const std::string& message) const {
    json::object r;
    r["jsonrpc"] = "2.0";
    r["id"] = id;
    json::object e;
    e["code"] = code;
    e["message"] = message;
    r["error"] = e;
    return r;
}

json::value ApiController::okResponse(const json::value& id, const json::value& result) const {
    json::object r;
    r["jsonrpc"] = "2.0";
    r["id"] = id;
    r["result"] = result;
    return r;
}

HttpJsonServer::HttpJsonServer(application::ApplicationCore& appCore, std::string bindAddress, std::uint16_t port)
    : appCore_(appCore), bindAddress_(std::move(bindAddress)), port_(port) {}

HttpJsonServer::~HttpJsonServer() {
    stop();
}

void HttpJsonServer::start() {
    if (running_) {
        return;
    }

    running_ = true;
    acceptor_ = std::make_unique<tcp::acceptor>(ioContext_, tcp::endpoint{boost::asio::ip::make_address(bindAddress_), port_});

    serverThread_ = std::thread([this]() { acceptLoop(); });
}

void HttpJsonServer::stop() {
    if (!running_) {
        return;
    }

    running_ = false;
    boost::system::error_code ec;
    if (acceptor_) {
        acceptor_->cancel(ec);
        acceptor_->close(ec);
    }
    ioContext_.stop();

    if (serverThread_.joinable()) {
        serverThread_.join();
    }
}

void HttpJsonServer::acceptLoop() {
    while (running_) {
        boost::system::error_code ec;
        tcp::socket socket(ioContext_);
        acceptor_->accept(socket, ec);
        if (ec) {
            continue;
        }
        handleSession(std::move(socket));
    }
}

void HttpJsonServer::handleSession(tcp::socket socket) {
    beast::flat_buffer buffer;
    http::request<http::string_body> req;
    boost::system::error_code ec;

    http::read(socket, buffer, req, ec);
    if (ec) {
        return;
    }

    http::response<http::string_body> res;
    res.version(req.version());
    res.keep_alive(false);
    res.set(http::field::content_type, "application/json");
    
    res.set(http::field::access_control_allow_origin, "*");
    res.set(http::field::access_control_allow_methods, "POST, OPTIONS, GET");
    res.set(http::field::access_control_allow_headers, "Content-Type, Accept");
    res.set(http::field::access_control_max_age, "86400");
    
    if (req.method() == http::verb::options) {
        res.result(http::status::no_content);  // 204 No Content
        res.body().clear();
        res.prepare_payload();
        http::write(socket, res, ec);
        return;
    }
    
    if (req.method() != http::verb::post) {
        res.result(http::status::method_not_allowed);
        res.body() = R"({"jsonrpc":"2.0","id":null,"error":{"code":-32600,"message":"Only POST method is supported"}})";
        res.prepare_payload();
        http::write(socket, res, ec);
        return;
    }
    
    json::value payload;
    try {
        payload = json::parse(req.body());
    } catch (const std::exception& e) {
        res.result(http::status::bad_request);
        // 🔥 Ошибка парсинга в формате JSON-RPC 2.0
        res.body() = R"({"jsonrpc":"2.0","id":null,"error":{"code":-32700,"message":"Parse error: invalid JSON"}})";
        res.prepare_payload();
        http::write(socket, res, ec);
        return;
    }
    
    ApiController controller(appCore_);
    const auto response = controller.processRequest(payload);
    
    res.result(http::status::ok);
    res.body() = json::serialize(response);  // boost::json → string
    res.prepare_payload();
    
    http::write(socket, res, ec);
    
    socket.shutdown(tcp::socket::shutdown_both, ec);
}

} // namespace api
