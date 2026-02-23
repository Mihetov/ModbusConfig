#pragma once

#include <QJsonObject>

#include "core/protocol/ModbusProtocolService.h"

namespace application {

class ModbusApplicationService
{
public:
    explicit ModbusApplicationService(protocol::ModbusProtocolService& protocolService);

    QJsonObject handle(const QJsonObject& request) const;

private:
    protocol::ModbusProtocolService& m_protocolService;

    static QJsonObject makeError(const QString& requestId, const QString& error);
    static QJsonObject makeSuccess(const QString& requestId, const protocol::ModbusResult& result);
};

} // namespace application
