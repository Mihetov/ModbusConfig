#pragma once

#include "core/transport/IModbusTransport.h"

namespace transport {

class MazurelModbusTransport final : public IModbusTransport
{
public:
    protocol::ModbusResult execute(const protocol::ModbusCommand& command) override;
};

} // namespace transport
