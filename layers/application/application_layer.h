#pragma once

#include <memory>
#include <vector>
#include <boost/json.hpp>
#include "layers/protocol/protocol_layer.h"
#include "layers/transport/transport_layer.h"

class ApplicationCore : public QObject {
    Q_OBJECT
public:
    explicit ApplicationCore(TransportManager* transportManager, QObject* parent = nullptr);

    void handleJsonRequest(const boost::json::value& request);
    void sendModbusCommand(const MBCommand& cmd, std::shared_ptr<Session> session);

signals:
    void jsonResponseReady(const boost::json::value& response);

private slots:
    void onFrameReceived(const std::vector<uint8_t>& frame, std::shared_ptr<Session> session);

private:
    TransportManager* transportManager_;
    ProtocolHandler protocolHandler_;
};
