#include <chrono>
#include <cstdint>
#include <iostream>
#include <limits>
#include <optional>
#include <string>
#include <thread>
#include <unordered_map>

#include "layers/api/api_layer.h"
#include "layers/application/application_layer.h"
#include "layers/transport/transport_layer.h"

namespace {

struct StartupOptions {
    std::string mode = "api";                 // api | headless
    std::string bindAddress = "0.0.0.0";
    std::uint16_t apiPort = 8080;

    std::string startupTransport = "none";    // none | tcp | rtu
    std::string tcpHost = "127.0.0.1";
    std::uint16_t tcpPort = 502;

    std::string rtuPort;
    std::uint32_t rtuBaud = 9600;
    std::uint8_t rtuStopBits = 1;

    bool verboseModbus = false;
    bool showHelp = false;
};

void printUsage() {
    std::cout
        << "Usage: ModbusConfig [options]\n"
        << "Options:\n"
        << "  --mode <api|headless>          Run mode (default: api)\n"
        << "  --bind <ip>                    API bind address (default: 0.0.0.0)\n"
        << "  --api-port <port>              API TCP port (default: 8080)\n"
        << "  --transport <none|tcp|rtu>     Transport opened on startup (default: none)\n"
        << "\n"
        << "  TCP startup parameters:\n"
        << "    --tcp-host <ip>              TCP host (default: 127.0.0.1)\n"
        << "    --tcp-port <port>            TCP port (default: 502)\n"
        << "\n"
        << "  RTU startup parameters:\n"
        << "    --rtu-port <path_or_name>    Serial port, e.g. /dev/ttyUSB0 or COM3\n"
        << "    --rtu-baud <rate>            Baud rate (default: 9600)\n"
        << "    --rtu-stop-bits <1|2>        Stop bits (default: 1)\n"
        << "\n"
        << "  Other:\n"
        << "    --verbose-modbus             Print incoming Modbus JSON responses\n"
        << "    --help                       Show this help\n";
}

template <typename UInt>
bool parseUnsigned(const std::string& text, UInt& out) {
    try {
        unsigned long long value = std::stoull(text);
        if (value > static_cast<unsigned long long>(std::numeric_limits<UInt>::max())) {
            return false;
        }
        out = static_cast<UInt>(value);
        return true;
    } catch (...) {
        return false;
    }
}

std::optional<StartupOptions> parseArgs(int argc, char* argv[], std::string& error) {
    StartupOptions options;

    for (int i = 1; i < argc; ++i) {
        const std::string arg = argv[i];

        auto getValue = [&](const std::string& key) -> std::optional<std::string> {
            if (i + 1 >= argc) {
                error = "Missing value for " + key;
                return std::nullopt;
            }
            ++i;
            return std::string(argv[i]);
        };

        if (arg == "--help") {
            options.showHelp = true;
            continue;
        }
        if (arg == "--verbose-modbus") {
            options.verboseModbus = true;
            continue;
        }
        if (arg == "--mode") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            options.mode = *value;
            continue;
        }
        if (arg == "--bind") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            options.bindAddress = *value;
            continue;
        }
        if (arg == "--api-port") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            if (!parseUnsigned(*value, options.apiPort)) {
                error = "Invalid --api-port value: " + *value;
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--transport") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            options.startupTransport = *value;
            continue;
        }
        if (arg == "--tcp-host") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            options.tcpHost = *value;
            continue;
        }
        if (arg == "--tcp-port") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            if (!parseUnsigned(*value, options.tcpPort)) {
                error = "Invalid --tcp-port value: " + *value;
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--rtu-port") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            options.rtuPort = *value;
            continue;
        }
        if (arg == "--rtu-baud") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            if (!parseUnsigned(*value, options.rtuBaud)) {
                error = "Invalid --rtu-baud value: " + *value;
                return std::nullopt;
            }
            continue;
        }
        if (arg == "--rtu-stop-bits") {
            auto value = getValue(arg);
            if (!value) return std::nullopt;
            if (!parseUnsigned(*value, options.rtuStopBits)) {
                error = "Invalid --rtu-stop-bits value: " + *value;
                return std::nullopt;
            }
            continue;
        }

        error = "Unknown argument: " + arg;
        return std::nullopt;
    }

    if (options.mode != "api" && options.mode != "headless") {
        error = "Unsupported --mode. Use api or headless";
        return std::nullopt;
    }

    if (options.startupTransport != "none" && options.startupTransport != "tcp" && options.startupTransport != "rtu") {
        error = "Unsupported --transport. Use none, tcp, or rtu";
        return std::nullopt;
    }

    if (options.startupTransport == "rtu" && options.rtuPort.empty()) {
        error = "--rtu-port is required when --transport rtu";
        return std::nullopt;
    }

    if (options.rtuStopBits != 1 && options.rtuStopBits != 2) {
        error = "--rtu-stop-bits must be 1 or 2";
        return std::nullopt;
    }

    return options;
}

bool openStartupTransport(application::ApplicationCore& appCore, const StartupOptions& options) {
    if (options.startupTransport == "none") {
        return true;
    }

    std::string error;
    bool opened = false;

    if (options.startupTransport == "tcp") {
        opened = appCore.openTcpTransport(options.tcpHost, options.tcpPort, error);
    } else {
        opened = appCore.openRtuTransport(options.rtuPort, options.rtuBaud, options.rtuStopBits, error);
    }

    if (!opened) {
        std::cerr << "Failed to open startup transport: " << error << std::endl;
    }
    return opened;
}

} // namespace

int main(int argc, char* argv[]) {
    std::string parseError;
    const auto parsed = parseArgs(argc, argv, parseError);
    if (!parsed) {
        std::cerr << parseError << "\n\n";
        printUsage();
        return 2;
    }

    const auto options = *parsed;
    if (options.showHelp) {
        printUsage();
        return 0;
    }

    transport::TransportManager transportManager;
    application::ApplicationCore appCore(transportManager);

    if (options.verboseModbus) {
        appCore.setJsonResponseCallback([](const boost::json::value& response) {
            std::cout << "[modbus-response] " << boost::json::serialize(response) << std::endl;
        });
    }

    if (!openStartupTransport(appCore, options)) {
        return 1;
    }

    std::cout << "Mode: " << options.mode << std::endl;
    std::cout << "Startup transport: " << options.startupTransport << std::endl;

    std::optional<api::HttpJsonServer> server;
    if (options.mode == "api") {
        server.emplace(appCore, options.bindAddress, options.apiPort);
        server->start();
        std::cout << "HTTP JSON API started on " << options.bindAddress << ':' << options.apiPort << std::endl;
    }

    while (true) {
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }

    return 0;
}
