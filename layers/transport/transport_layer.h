#pragma once

#include <array>
#include <atomic>
#include <cstdint>
#include <deque>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <unordered_map>
#include <variant>
#include <vector>

#include <boost/asio.hpp>
#include <boost/asio/serial_port.hpp>

namespace transport {

using boost::asio::ip::tcp;

enum class ConnectionType {
    Tcp,
    Rtu
};

class Session;
using SessionPtr = std::shared_ptr<Session>;
using FrameCallback = std::function<void(const std::vector<uint8_t>&, const SessionPtr&)>;
using ConnectionCallback = std::function<void(bool connected, const SessionPtr&)>;
using ErrorCallback = std::function<void(const std::string&)>;

class Session : public std::enable_shared_from_this<Session> {
public:
    Session(std::uint64_t id, tcp::socket socket);
    Session(std::uint64_t id, boost::asio::serial_port port);

    std::uint64_t id() const noexcept;
    ConnectionType connectionType() const noexcept;

    void start(FrameCallback onFrame, ErrorCallback onError);
    void send(const std::vector<uint8_t>& data, ErrorCallback onError);
    void close();

private:
    void doRead();
    void doWrite();

    std::uint64_t id_;
    std::variant<tcp::socket, boost::asio::serial_port> stream_;
    std::array<std::uint8_t, 2048> readBuffer_{};
    std::deque<std::vector<std::uint8_t>> writeQueue_;
    FrameCallback onFrame_;
    ErrorCallback onError_;
    bool closed_ = false;
};

class TransportManager {
public:
    TransportManager();
    ~TransportManager();

    TransportManager(const TransportManager&) = delete;
    TransportManager& operator=(const TransportManager&) = delete;

    SessionPtr connectTcpSlave(const std::string& ip, std::uint16_t port);
    SessionPtr connectSerialSlave(const std::string& portName, std::uint32_t baudRate = 9600);

    void sendToSession(const std::vector<uint8_t>& data, const SessionPtr& session);
    void disconnectSession(std::uint64_t sessionId);
    void disconnectAll();

    bool hasActiveConnections() const;
    SessionPtr getFirstConnection() const;
    std::vector<SessionPtr> getAllConnections() const;

    void setFrameCallback(FrameCallback cb);
    void setConnectionCallback(ConnectionCallback cb);
    void setErrorCallback(ErrorCallback cb);

private:
    void notifyConnected(const SessionPtr& session);
    void notifyDisconnected(const SessionPtr& session);
    void notifyError(const std::string& error) const;

    boost::asio::io_context ioContext_;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> workGuard_;
    std::thread ioThread_;

    mutable std::mutex sessionsMutex_;
    std::unordered_map<std::uint64_t, SessionPtr> sessions_;
    std::atomic<std::uint64_t> nextSessionId_{1};

    FrameCallback onFrame_;
    ConnectionCallback onConnection_;
    ErrorCallback onError_;
};

} // namespace transport
