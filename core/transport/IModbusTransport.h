#pragma once

#include "core/protocol/ModbusProtocol.h"

namespace transport {

class IModbusTransport
{
public:
    virtual ~IModbusTransport() = default;
    virtual protocol::ModbusResult execute(const protocol::ModbusCommand& command) = 0;
};

} // namespace transport
