#pragma once
#include <vector>
#include <functional>
#include <cstdint>

class ITransport
{
public:
    using ReceiveCallback = std::function<void(std::vector<uint8_t>)>;
    using ErrorCallback   = std::function<void(const std::string&)>;

    virtual ~ITransport() = default;

    virtual void connect(const std::string& host, uint16_t port) = 0;
    virtual void disconnect() = 0;

    virtual void send(const std::vector<uint8_t>& data) = 0;

    virtual void setReceiveCallback(ReceiveCallback cb) = 0;
    virtual void setErrorCallback(ErrorCallback cb) = 0;
};
