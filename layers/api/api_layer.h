#pragma once

#include <boost/asio.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/json.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#include "layers/application/application_layer.h"

namespace api {

class ApiController {
public:
    explicit ApiController(application::ApplicationCore& appCore);

    boost::json::value processRequest(const boost::json::value& request);
    boost::json::array processBatch(const boost::json::array& requests);

private:
    boost::json::value processSingle(const boost::json::object& req);
    boost::json::value errorResponse(const boost::json::value& id, int code, const std::string& message) const;
    boost::json::value okResponse(const boost::json::value& id, const boost::json::value& result) const;

    application::ApplicationCore& appCore_;
};

class HttpJsonServer {
public:
    HttpJsonServer(application::ApplicationCore& appCore, std::string bindAddress, std::uint16_t port);
    ~HttpJsonServer();

    void start();
    void stop();

private:
    void acceptLoop();
    void handleSession(boost::asio::ip::tcp::socket socket);

    application::ApplicationCore& appCore_;
    std::string bindAddress_;
    std::uint16_t port_;

    boost::asio::io_context ioContext_;
    std::unique_ptr<boost::asio::ip::tcp::acceptor> acceptor_;
    std::atomic<bool> running_{false};
    std::thread serverThread_;
};

} // namespace api
