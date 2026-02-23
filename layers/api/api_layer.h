#pragma once
#include <QObject>
#include <QByteArray>
#include <memory>
#include <boost/json.hpp>
#include "application/application_layer.h"
#include "transport/transport_layer.h" // Для доступа к Session

namespace json = boost::json;

class ApiController : public QObject {
    Q_OBJECT
public:
    explicit ApiController(ApplicationCore *core, QObject *parent = nullptr);

    void processRequest(const json::value &request, std::shared_ptr<Session> session); // Исправлено

signals:
    void responseReady(const QByteArray &response, std::shared_ptr<Session> session); // Исправлено
    void modbusRequestOut(const QByteArray &frame, std::shared_ptr<Session> session); // Исправлено

private:
    ApplicationCore *m_core;

    json::value createResponse(int id, const json::value &result);
    json::value createError(int id, int code, const QString &message);
};
