#include <chrono>
#include <iostream>
#include <thread>

#include "layers/api/api_layer.h"
#include "layers/application/application_layer.h"
#include "layers/transport/transport_layer.h"

int main() {
    transport::TransportManager transportManager;
    application::ApplicationCore appCore(transportManager);

    appCore.setJsonResponseCallback([](const boost::json::value& response) {
        std::cout << "[modbus-response] " << boost::json::serialize(response) << std::endl;
    });

    api::HttpJsonServer server(appCore, "0.0.0.0", 8080);
    server.start();

    std::cout << "HTTP JSON API started on 0.0.0.0:8080" << std::endl;
    std::cout << "Use POST with JSON-RPC object or array." << std::endl;

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    return 0;
}
