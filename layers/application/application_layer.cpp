#include "application_layer.h"
#include <QDebug>

// Класс-адаптер для обработки событий транспорта
class TransportEventHandlerImpl : public TransportEventHandler {
public:
    TransportEventHandlerImpl(ApplicationCore* appCore) : appCore_(appCore) {}

    void onFrameReceived(const std::vector<uint8_t>& frame, std::shared_ptr<Session> session) override {
        if (appCore_) {
            appCore_->onFrameReceived(frame, session);
        }
    }

    void onConnectionStatusChanged(bool connected) override {
        if (appCore_) {
            // Можно добавить обработку изменения статуса соединения
            qDebug() << "Connection status changed:" << connected;
        }
    }

private:
    ApplicationCore* appCore_;
};

ApplicationCore::ApplicationCore(TransportManager* transportManager, QObject* parent)
    : QObject(parent), transportManager_(transportManager) {

    // Создаем и устанавливаем обработчик событий
    auto eventHandler = new TransportEventHandlerImpl(this);
    transportManager_->setEventHandler(eventHandler);
}

void ApplicationCore::sendModbusCommand(const MBCommand& cmd, std::shared_ptr<Session> session) {
    ConnectionType type = session->getConnectionType();
    // Получаем сырые байты из Protocol Layer
    auto rawFrame = protocolHandler_.createModbusFrame(cmd, type);
    // Отправляем через Transport Layer
    transportManager_->sendToSession(rawFrame, session);
}

void ApplicationCore::onFrameReceived(const std::vector<uint8_t>& frame, std::shared_ptr<Session> session) {
    try {
        ConnectionType type = session->getConnectionType();
        int requestId = 0;
        // Обрабатываем буфер (может содержать несколько фреймов)
        auto jsonResponses = protocolHandler_.processIncomingBuffer(frame, type, requestId);
        for (const auto& jsonResponse : jsonResponses) {
            emit jsonResponseReady(jsonResponse);
        }
    }
    catch (const std::exception& e) {
        qWarning() << "Error processing frame:" << e.what();
    }
}

void ApplicationCore::handleJsonRequest(const boost::json::value& request) {
    MBCommand cmd;
    std::string error;
    if (!protocolHandler_.jsonToMBCommand(request, cmd, error)) {
        // Формирование ошибки
        boost::json::object errorResponse;
        errorResponse["jsonrpc"] = "2.0";
        errorResponse["id"] = request.as_object().contains("id") ? request.at("id") : nullptr;

        boost::json::object errObj;
        errObj["code"] = -32600; // Invalid Request
        errObj["message"] = error;

        errorResponse["error"] = errObj;

        emit jsonResponseReady(errorResponse);
        return;
    }
    // Определение соединения и отправка команды
    if (transportManager_->hasActiveConnections()) {
        auto connection = transportManager_->getFirstConnection();
        sendModbusCommand(cmd, connection);
    } else {
        boost::json::object errorResponse;
        errorResponse["jsonrpc"] = "2.0";
        errorResponse["id"] = request.as_object().contains("id") ? request.at("id") : nullptr;

        boost::json::object errObj;
        errObj["code"] = -32603; // Internal error
        errObj["message"] = "No active Modbus connection";

        errorResponse["error"] = errObj;

        emit jsonResponseReady(errorResponse);
    }
}
