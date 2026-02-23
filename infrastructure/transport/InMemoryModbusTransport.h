#pragma once

#include <QHash>

#include "core/transport/IModbusTransport.h"

namespace transport {

class InMemoryModbusTransport final : public IModbusTransport
{
public:
    protocol::ModbusResult execute(const protocol::ModbusCommand& command) override;

private:
    QHash<int, int> m_registers;

    protocol::ModbusResult readHoldingRegisters(const protocol::ModbusCommand& command);
    protocol::ModbusResult writeSingleRegister(const protocol::ModbusCommand& command);
    protocol::ModbusResult writeMultipleRegisters(const protocol::ModbusCommand& command);
};

} // namespace transport
