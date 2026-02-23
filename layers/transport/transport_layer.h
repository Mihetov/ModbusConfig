#pragma once

#include <memory>
#include <vector>
#include <functional>
#include <string>
#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>
#include <variant>
#include <type_traits>

using boost::asio::ip::tcp;

// Перечисление для типов соединения


// Объявляем TransportManager для использования в Session
class TransportManager;

// Интерфейс для обработки событий транспорта
class TransportEventHandler {
public:
    virtual ~TransportEventHandler() = default;

    // Вызывается при получении фрейма
    virtual void onFrameReceived(const std::vector<uint8_t>& frame, std::shared_ptr<Session> session) = 0;

    // Вызывается при изменении статуса соединения
    virtual void onConnectionStatusChanged(bool connected) = 0;
};

class Session : public std::enable_shared_from_this<Session> {
public:
    explicit Session(tcp::socket socket, TransportManager* manager);
    explicit Session(boost::asio::serial_port port, TransportManager* manager);
    void start();
    void send(const std::vector<uint8_t>& data);

    bool isTCPConnection() const;
    bool isSerialConnection() const;

    // Добавляем метод для получения типа соединения
    ConnectionType getConnectionType() const {
        return isTCPConnection() ? ConnectionType::TCP : ConnectionType::RTU;
    }

private:
    void doTcpRead();
    void doSerialRead();
    boost::asio::streambuf buffer_;
    TransportManager* manager_;
    // Используем variant для поддержки обоих типов соединений
    std::variant<tcp::socket, boost::asio::serial_port> socketOrPort_;
};

class TransportManager {
public:
    explicit TransportManager();
    ~TransportManager();

    // Устанавливаем обработчик событий
    void setEventHandler(TransportEventHandler* handler);

    // Методы для Master-режима (клиент)
    bool connectToTcpSlave(const std::string& ip, uint16_t port);
    bool connectToSerialSlave(const std::string& portName, uint32_t baudRate = 9600);

    bool hasActiveConnections() const;
    std::shared_ptr<Session> getFirstConnection() const;
    std::vector<std::shared_ptr<Session>> getAllConnections() const;

    void disconnect();
    void sendToSession(const std::vector<uint8_t>& data, std::shared_ptr<Session> session);

private:
    void handleConnectionError(const std::string& error);

    boost::asio::io_context ioContext_;
    std::vector<std::shared_ptr<Session>> connections_;
    TransportEventHandler* eventHandler_ = nullptr;

    // Для запуска io_context в отдельном потоке
    std::thread ioThread_;
    bool running_ = false;
};
