#include "ConsoleJsonApi.h"

#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

namespace api {

ConsoleJsonApi::ConsoleJsonApi(application::ModbusApplicationService& applicationService)
    : m_applicationService(applicationService)
{
}

QByteArray ConsoleJsonApi::handleLine(const QByteArray& jsonLine) const
{
    QJsonParseError parseError;
    const QJsonDocument document = QJsonDocument::fromJson(jsonLine, &parseError);

    if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
        const QJsonObject errorResponse = {
            {"status", "error"},
            {"error", QString("Invalid JSON: %1").arg(parseError.errorString())}
        };
        return QJsonDocument(errorResponse).toJson(QJsonDocument::Compact);
    }

    const QJsonObject response = m_applicationService.handle(document.object());
    return QJsonDocument(response).toJson(QJsonDocument::Compact);
}

} // namespace api
