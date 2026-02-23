#pragma once
#include <vector>
#include <cstdint>

struct ModbusRequest
{
    uint8_t  slaveId;
    uint8_t  functionCode;
    uint16_t address;
    uint16_t quantity;
    std::vector<uint16_t> values;
};

struct ModbusResponse
{
    uint8_t  slaveId;
    uint8_t  functionCode;
    std::vector<uint16_t> values;
    bool     isException = false;
    uint8_t  exceptionCode = 0;
};
