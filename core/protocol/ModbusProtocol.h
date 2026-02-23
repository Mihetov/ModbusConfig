#pragma once

#include <QList>
#include <QString>

namespace protocol {

enum class FunctionCode
{
    ReadHoldingRegisters,
    WriteSingleRegister,
    WriteMultipleRegisters
};

struct ModbusCommand
{
    FunctionCode functionCode;
    int unitId = 1;
    int address = 0;
    int count = 0;
    int value = 0;
    QList<int> values;
};

struct ModbusResult
{
    bool success = false;
    QString error;
    QList<int> values;
};

} // namespace protocol
