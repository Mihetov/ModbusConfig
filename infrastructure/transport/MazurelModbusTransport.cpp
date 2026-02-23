#include "MazurelModbusTransport.h"

namespace transport {

protocol::ModbusResult MazurelModbusTransport::execute(const protocol::ModbusCommand&)
{
    return {
        false,
        "Mazurel/Modbus transport is not linked in this environment. Implement this adapter after adding the library sources.",
        {}
    };
}

} // namespace transport
