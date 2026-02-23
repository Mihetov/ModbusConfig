#include <iostream>

#include <boost/json.hpp>

#include "layers/application/application_layer.h"
#include "layers/transport/transport_layer.h"

int main() {
    transport::TransportManager transportManager;
    application::ApplicationCore appCore(transportManager);

    appCore.setJsonResponseCallback([](const boost::json::value& response) {
        std::cout << boost::json::serialize(response) << std::endl;
    });

    std::cout << "Modbus core initialized. API layer is not connected in this executable." << std::endl;
    return 0;
}
