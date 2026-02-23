#pragma once
#include "ModbusTypes.h"

class IModbusCodec
{
public:
    virtual ~IModbusCodec() = default;

    virtual std::vector<uint8_t> encode(const ModbusRequest& request) = 0;
    virtual ModbusResponse decode(const std::vector<uint8_t>& frame) = 0;
};
