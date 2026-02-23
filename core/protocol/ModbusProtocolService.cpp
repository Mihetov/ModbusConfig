#include "ModbusProtocolService.h"

namespace protocol {

ModbusProtocolService::ModbusProtocolService(transport::IModbusTransport& transport)
    : m_transport(transport)
{
}

ModbusResult ModbusProtocolService::execute(const ModbusCommand& command) const
{
    return m_transport.execute(command);
}

} // namespace protocol
