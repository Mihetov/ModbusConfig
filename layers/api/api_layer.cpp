#include "api_layer.h"

#include <boost/beast/version.hpp>

namespace api {

namespace beast = boost::beast;
namespace http = beast::http;
namespace json = boost::json;
using tcp = boost::asio::ip::tcp;

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
        std::string error;
        const bool input = params.contains("input") && params.at("input").as_bool();
        const bool ok = appCore_.readRegisters(
            static_cast<std::uint8_t>(params.at("slave_id").as_int64()),
            static_cast<std::uint16_t>(params.at("address").as_int64()),
            static_cast<std::uint16_t>(params.at("count").as_int64()),
            input,
            error);
        if (!ok) {
            return errorResponse(id, -32002, error);
        }
        return okResponse(id, json::object{{"accepted", true}});
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
            req.slaveId = static_cast<std::uint8_t>(r.at("slave_id").as_int64());
            req.startAddress = static_cast<std::uint16_t>(r.at("address").as_int64());
            req.count = static_cast<std::uint16_t>(r.at("count").as_int64());
            req.function = r.contains("input") && r.at("input").as_bool()
                               ? protocol::FunctionCode::ReadInputRegisters
                               : protocol::FunctionCode::ReadHoldingRegisters;
            requests.push_back(req);
        }

        std::string error;
        if (!appCore_.readGroup(requests, error)) {
            return errorResponse(id, -32002, error);
        }
        return okResponse(id, json::object{{"accepted", true}, {"count", requests.size()}});
    }

    if (method == "modbus.write") {
        if (!params.contains("slave_id") || !params.contains("address")) {
            return errorResponse(id, -32602, "slave_id and address are required");
        }

        std::string error;
        bool ok = false;
        if (params.contains("values") && params.at("values").is_array()) {
            std::vector<std::uint16_t> values;
            for (const auto& v : params.at("values").as_array()) {
                values.push_back(static_cast<std::uint16_t>(v.as_int64()));
            }
            ok = appCore_.writeMultipleRegisters(
                static_cast<std::uint8_t>(params.at("slave_id").as_int64()),
                static_cast<std::uint16_t>(params.at("address").as_int64()),
                values,
                error);
        } else if (params.contains("value")) {
            ok = appCore_.writeSingleRegister(
                static_cast<std::uint8_t>(params.at("slave_id").as_int64()),
                static_cast<std::uint16_t>(params.at("address").as_int64()),
                static_cast<std::uint16_t>(params.at("value").as_int64()),
                error);
        } else {
            return errorResponse(id, -32602, "value or values required");
        }

        if (!ok) {
            return errorResponse(id, -32003, error);
        }
        return okResponse(id, json::object{{"accepted", true}});
    }

    if (method == "modbus.write_group") {
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
            req.slaveId = static_cast<std::uint8_t>(r.at("slave_id").as_int64());
            req.startAddress = static_cast<std::uint16_t>(r.at("address").as_int64());
            if (r.contains("values") && r.at("values").is_array()) {
                req.function = protocol::FunctionCode::WriteMultipleRegisters;
                for (const auto& v : r.at("values").as_array()) {
                    req.values.push_back(static_cast<std::uint16_t>(v.as_int64()));
                }
                req.count = static_cast<std::uint16_t>(req.values.size());
            } else {
                req.function = protocol::FunctionCode::WriteSingleRegister;
                req.values.push_back(static_cast<std::uint16_t>(r.at("value").as_int64()));
            }
            requests.push_back(req);
        }

        std::string error;
        if (!appCore_.writeGroup(requests, error)) {
            return errorResponse(id, -32003, error);
        }
        return okResponse(id, json::object{{"accepted", true}, {"count", requests.size()}});
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

    if (req.method() != http::verb::post) {
        res.result(http::status::method_not_allowed);
        res.body() = R"({"error":"Only POST is supported"})";
        res.prepare_payload();
        http::write(socket, res, ec);
        return;
    }

    json::value payload;
    try {
        payload = json::parse(req.body());
    } catch (...) {
        res.result(http::status::bad_request);
        res.body() = R"({"error":"Invalid JSON"})";
        res.prepare_payload();
        http::write(socket, res, ec);
        return;
    }

    ApiController controller(appCore_);
    const auto response = controller.processRequest(payload);

    res.result(http::status::ok);
    res.body() = json::serialize(response);
    res.prepare_payload();
    http::write(socket, res, ec);

    socket.shutdown(tcp::socket::shutdown_both, ec);
}

} // namespace api
