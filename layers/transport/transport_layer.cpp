#include "transport_layer.h"
#include <iostream>
#include <algorithm>

// Реализация Session
Session::Session(tcp::socket socket, TransportManager* manager)
    : buffer_(), manager_(manager), socketOrPort_(std::move(socket)) {}

Session::Session(boost::asio::serial_port port, TransportManager* manager)
    : buffer_(), manager_(manager), socketOrPort_(std::move(port)) {}

void Session::start() {
    std::visit([this](auto& socket) {
        if constexpr (std::is_same_v<std::decay_t<decltype(socket)>, tcp::socket>) {
            doTcpRead();
        } else {
            doSerialRead();
        }
    }, socketOrPort_);
}

void Session::send(const std::vector<uint8_t>& data) {
    std::visit([this, &data](auto& socket) {
        boost::asio::async_write(socket, boost::asio::buffer(data.data(), data.size()),
                                 [this](const boost::system::error_code &error, std::size_t) {
                                     if (error && manager_) {
                                         std::cerr << "Write error: " << error.message() << std::endl;
                                     }
                                 });
    }, socketOrPort_);
}

void Session::doTcpRead() {
    auto self = shared_from_this();
    // Убедимся, что это TCP-соединение
    if (!isTCPConnection()) {
        return;
    }
    // Используем std::get для извлечения сокета из variant
    tcp::socket& socket = std::get<tcp::socket>(socketOrPort_);
    // Читаем минимальное количество данных
    boost::asio::async_read(socket,
                            buffer_,
                            boost::asio::transfer_at_least(1),
                            [this, self](const boost::system::error_code& ec, std::size_t bytes_transferred) {
                                if (ec) {
                                    std::cerr << "TCP Read error: " << ec.message() << std::endl;
                                    return;
                                }
                                // Создаем копию буфера для захвата в лямбду
                                std::istream is(&buffer_);
                                std::vector<uint8_t> data(bytes_transferred);
                                is.read(reinterpret_cast<char*>(data.data()), bytes_transferred);

                                // Отправляем данные через обработчик событий
                                if (manager_ && manager_->eventHandler_) {
                                    manager_->eventHandler_->onFrameReceived(data, shared_from_this());
                                }

                                // Продолжаем чтение
                                doTcpRead();
                            });
}

void Session::doSerialRead() {
    auto self = shared_from_this();
    boost::asio::async_read(std::get<boost::asio::serial_port>(socketOrPort_), buffer_,
                            boost::asio::transfer_at_least(1),
                            [this, self](const boost::system::error_code &error, std::size_t bytes_transferred) {
                                if (!error) {
                                    std::istream is(&buffer_);
                                    std::vector<uint8_t> data(bytes_transferred);
                                    is.read(reinterpret_cast<char*>(data.data()), bytes_transferred);

                                    // Отправляем данные через обработчик событий
                                    if (manager_ && manager_->eventHandler_) {
                                        manager_->eventHandler_->onFrameReceived(data, shared_from_this());
                                    }

                                    doSerialRead();
                                } else {
                                    std::cerr << "Serial Read error: " << error.message() << std::endl;
                                }
                            });
}

bool Session::isTCPConnection() const {
    return std::holds_alternative<tcp::socket>(socketOrPort_);
}

bool Session::isSerialConnection() const {
    return std::holds_alternative<boost::asio::serial_port>(socketOrPort_);
}

// Реализация TransportManager
TransportManager::TransportManager() {
    running_ = true;
    ioThread_ = std::thread(&TransportManager::runIoContext, this);
}

TransportManager::~TransportManager() {
    running_ = false;
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

void TransportManager::runIoContext() {
    ioContext_.run();
}

void TransportManager::setEventHandler(TransportEventHandler* handler) {
    eventHandler_ = handler;
}

bool TransportManager::connectToTcpSlave(const std::string& ip, uint16_t port) {
    try {
        // Создаем TCP-сокет
        tcp::socket socket(ioContext_);
        // Подключаемся к Modbus-устройству (Slave)
        socket.connect(tcp::endpoint(boost::asio::ip::address::from_string(ip), port));

        // Создаем сессию
        auto session = std::make_shared<Session>(std::move(socket), this);
        connections_.push_back(session);
        session->start();

        std::cout << "Connected to Modbus TCP device at " << ip << " port " << port << std::endl;

        if (eventHandler_) {
            eventHandler_->onConnectionStatusChanged(true);
        }

        return true;
    } catch (const boost::system::system_error& e) {
        handleConnectionError("Failed to connect to TCP device: " + std::string(e.what()));
        return false;
    }
}

bool TransportManager::connectToSerialSlave(const std::string& portName, uint32_t baudRate) {
    try {
        // Создаем и настраиваем последовательный порт
        boost::asio::serial_port port(ioContext_, portName);
        port.set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        port.set_option(boost::asio::serial_port_base::character_size(8));
        port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));

        // Создаем сессию для последовательного порта
        auto session = std::make_shared<Session>(std::move(port), this);
        connections_.push_back(session);
        session->start();

        std::cout << "Connected to Modbus RTU device on " << portName << " at " << baudRate << " baud" << std::endl;

        if (eventHandler_) {
            eventHandler_->onConnectionStatusChanged(true);
        }

        return true;
    } catch (const boost::system::system_error& e) {
        handleConnectionError("Failed to connect to serial device: " + std::string(e.what()));
        return false;
    }
}

void TransportManager::disconnect() {
    connections_.clear();
    if (eventHandler_) {
        eventHandler_->onConnectionStatusChanged(false);
    }
    std::cout << "Disconnected from all Modbus devices" << std::endl;
}

bool TransportManager::hasActiveConnections() const {
    return !connections_.empty();
}

std::shared_ptr<Session> TransportManager::getFirstConnection() const {
    if (!connections_.empty()) {
        return connections_.front();
    }
    return nullptr;
}

std::vector<std::shared_ptr<Session>> TransportManager::getAllConnections() const {
    return connections_;
}

void TransportManager::handleConnectionError(const std::string& error) {
    std::cerr << error << std::endl;
    if (eventHandler_) {
        eventHandler_->onConnectionStatusChanged(false);
    }
}

void TransportManager::sendToSession(const std::vector<uint8_t>& data, std::shared_ptr<Session> session) {
    // Проверяем, существует ли сессия в списке активных соединений
    auto it = std::find(connections_.begin(), connections_.end(), session);
    if (it != connections_.end()) {
        session->send(data);
    } else {
        std::cerr << "Attempt to send data to inactive session" << std::endl;
    }
}
