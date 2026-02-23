#include "InMemoryModbusTransport.h"

namespace transport {

protocol::ModbusResult InMemoryModbusTransport::execute(const protocol::ModbusCommand& command)
{
    switch (command.functionCode) {
    case protocol::FunctionCode::ReadHoldingRegisters:
        return readHoldingRegisters(command);
    case protocol::FunctionCode::WriteSingleRegister:
        return writeSingleRegister(command);
    case protocol::FunctionCode::WriteMultipleRegisters:
        return writeMultipleRegisters(command);
    }

    return {false, "Unsupported function code", {}};
}

protocol::ModbusResult InMemoryModbusTransport::readHoldingRegisters(const protocol::ModbusCommand& command)
{
    protocol::ModbusResult result;
    result.success = true;

    for (int offset = 0; offset < command.count; ++offset) {
        const int address = command.address + offset;
        result.values.append(m_registers.value(address, 0));
    }

    return result;
}

protocol::ModbusResult InMemoryModbusTransport::writeSingleRegister(const protocol::ModbusCommand& command)
{
    m_registers.insert(command.address, command.value);
    return {true, {}, {command.value}};
}

protocol::ModbusResult InMemoryModbusTransport::writeMultipleRegisters(const protocol::ModbusCommand& command)
{
    for (int offset = 0; offset < command.values.size(); ++offset) {
        m_registers.insert(command.address + offset, command.values.at(offset));
    }

    return {true, {}, command.values};
}

} // namespace transport
