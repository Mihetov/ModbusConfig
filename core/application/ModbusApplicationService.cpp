#include "ModbusApplicationService.h"

#include <QJsonArray>

namespace application {

ModbusApplicationService::ModbusApplicationService(protocol::ModbusProtocolService& protocolService)
    : m_protocolService(protocolService)
{
}

QJsonObject ModbusApplicationService::handle(const QJsonObject& request) const
{
    const QString requestId = request.value("requestId").toString();
    const QString commandName = request.value("command").toString();

    if (commandName.isEmpty()) {
        return makeError(requestId, "Field 'command' is required");
    }

    protocol::ModbusCommand command;
    command.unitId = request.value("unitId").toInt(1);
    command.address = request.value("address").toInt(-1);

    if (command.address < 0) {
        return makeError(requestId, "Field 'address' must be >= 0");
    }

    if (commandName == "read_holding_registers") {
        command.functionCode = protocol::FunctionCode::ReadHoldingRegisters;
        command.count = request.value("count").toInt(0);
        if (command.count <= 0) {
            return makeError(requestId, "Field 'count' must be > 0");
        }
    } else if (commandName == "write_single_register") {
        command.functionCode = protocol::FunctionCode::WriteSingleRegister;
        if (!request.contains("value")) {
            return makeError(requestId, "Field 'value' is required");
        }
        command.value = request.value("value").toInt();
    } else if (commandName == "write_multiple_registers") {
        command.functionCode = protocol::FunctionCode::WriteMultipleRegisters;
        const QJsonArray values = request.value("values").toArray();
        if (values.isEmpty()) {
            return makeError(requestId, "Field 'values' must contain at least one element");
        }
        for (const auto& value : values) {
            command.values.append(value.toInt());
        }
    } else {
        return makeError(requestId, QString("Unknown command '%1'").arg(commandName));
    }

    const protocol::ModbusResult result = m_protocolService.execute(command);
    if (!result.success) {
        return makeError(requestId, result.error);
    }

    return makeSuccess(requestId, result);
}

QJsonObject ModbusApplicationService::makeError(const QString& requestId, const QString& error)
{
    return {
        {"requestId", requestId},
        {"status", "error"},
        {"error", error}
    };
}

QJsonObject ModbusApplicationService::makeSuccess(const QString& requestId, const protocol::ModbusResult& result)
{
    QJsonObject data;
    QJsonArray values;
    for (const int value : result.values) {
        values.append(value);
    }
    data.insert("values", values);

    return {
        {"requestId", requestId},
        {"status", "ok"},
        {"data", data}
    };
}

} // namespace application
