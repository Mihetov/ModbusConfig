#pragma once
#include <string>
#include <memory>

class ITransport;
class IModbusCodec;

class Device
{
public:
    Device(uint8_t slaveId,
           std::unique_ptr<ITransport> transport,
           std::unique_ptr<IModbusCodec> codec);

    void connect(const std::string& host, uint16_t port);

    void readHoldingRegisters(uint16_t address, uint16_t quantity);
    void writeSingleRegister(uint16_t address, uint16_t value);

private:
    void handleIncoming(std::vector<uint8_t> data);

    uint8_t slaveId_;
    std::unique_ptr<ITransport> transport_;
    std::unique_ptr<IModbusCodec> codec_;
};
