#pragma once

#include <QByteArray>

#include "core/application/ModbusApplicationService.h"

namespace api {

class ConsoleJsonApi
{
public:
    explicit ConsoleJsonApi(application::ModbusApplicationService& applicationService);

    QByteArray handleLine(const QByteArray& jsonLine) const;

private:
    application::ModbusApplicationService& m_applicationService;
};

} // namespace api
