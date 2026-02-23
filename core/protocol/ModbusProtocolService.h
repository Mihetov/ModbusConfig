#pragma once

#include "core/protocol/ModbusProtocol.h"
#include "core/transport/IModbusTransport.h"

namespace protocol {

class ModbusProtocolService
{
public:
    explicit ModbusProtocolService(transport::IModbusTransport& transport);

    ModbusResult execute(const ModbusCommand& command) const;

private:
    transport::IModbusTransport& m_transport;
};

} // namespace protocol
