#include "transport_layer.h"

#include <sstream>

namespace transport {

namespace {

template <typename Stream>
void closeStream(Stream& stream) {
    boost::system::error_code ec;
    stream.cancel(ec);
    stream.close(ec);
}

} // namespace

Session::Session(std::uint64_t id, tcp::socket socket)
    : id_(id), stream_(std::move(socket)) {}

Session::Session(std::uint64_t id, boost::asio::serial_port port)
    : id_(id), stream_(std::move(port)) {}

std::uint64_t Session::id() const noexcept { return id_; }

ConnectionType Session::connectionType() const noexcept {
    return std::holds_alternative<tcp::socket>(stream_) ? ConnectionType::Tcp : ConnectionType::Rtu;
}

void Session::start(FrameCallback onFrame, ErrorCallback onError) {
    onFrame_ = std::move(onFrame);
    onError_ = std::move(onError);
    doRead();
}

void Session::send(const std::vector<uint8_t>& data, ErrorCallback onError) {
    if (data.empty() || closed_) {
        return;
    }

    auto self = shared_from_this();
    boost::asio::post(std::visit([](auto& stream) { return stream.get_executor(); }, stream_),
        [this, self, payload = std::vector<uint8_t>(data), onError = std::move(onError)]() mutable {
            const bool writeInProgress = !writeQueue_.empty();
            writeQueue_.push_back(std::move(payload));
            if (!writeInProgress) {
                doWrite();
            }
            if (!onError_) {
                onError_ = std::move(onError);
            }
        });
}

void Session::close() {
    closed_ = true;
    std::visit([](auto& stream) { closeStream(stream); }, stream_);
}

void Session::doRead() {
    if (closed_) {
        return;
    }

    auto self = shared_from_this();
    std::visit(
        [this, self](auto& stream) {
            stream.async_read_some(
                boost::asio::buffer(readBuffer_),
                [this, self](const boost::system::error_code& ec, std::size_t bytesRead) {
                    if (ec) {
                        closed_ = true;
                        if (ec != boost::asio::error::operation_aborted && onError_) {
                            onError_("Read error in session " + std::to_string(id_) + ": " + ec.message());
                        }
                        return;
                    }

                    std::vector<uint8_t> frame(readBuffer_.begin(), readBuffer_.begin() + bytesRead);
                    if (onFrame_) {
                        onFrame_(frame, self);
                    }
                    doRead();
                });
        },
        stream_);
}

void Session::doWrite() {
    if (writeQueue_.empty() || closed_) {
        return;
    }

    auto self = shared_from_this();
    std::visit(
        [this, self](auto& stream) {
            boost::asio::async_write(
                stream,
                boost::asio::buffer(writeQueue_.front()),
                [this, self](const boost::system::error_code& ec, std::size_t) {
                    if (ec) {
                        closed_ = true;
                        if (ec != boost::asio::error::operation_aborted && onError_) {
                            onError_("Write error in session " + std::to_string(id_) + ": " + ec.message());
                        }
                        return;
                    }

                    writeQueue_.pop_front();
                    doWrite();
                });
        },
        stream_);
}

TransportManager::TransportManager()
    : workGuard_(boost::asio::make_work_guard(ioContext_)),
      ioThread_([this]() { ioContext_.run(); }) {}

TransportManager::~TransportManager() {
    disconnectAll();
    workGuard_.reset();
    ioContext_.stop();
    if (ioThread_.joinable()) {
        ioThread_.join();
    }
}

SessionPtr TransportManager::connectTcpSlave(const std::string& ip, std::uint16_t port) {
    try {
        tcp::socket socket(ioContext_);
        socket.connect({boost::asio::ip::make_address(ip), port});

        auto session = std::make_shared<Session>(nextSessionId_++, std::move(socket));
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_.emplace(session->id(), session);
        }
        session->start(onFrame_, [this](const std::string& error) { notifyError(error); });
        notifyConnected(session);
        return session;
    } catch (const std::exception& e) {
        notifyError(std::string("TCP connect error: ") + e.what());
        return nullptr;
    }
}

SessionPtr TransportManager::connectSerialSlave(const std::string& portName, std::uint32_t baudRate) {
    try {
        boost::asio::serial_port port(ioContext_);
        port.open(portName);
        port.set_option(boost::asio::serial_port_base::baud_rate(baudRate));
        port.set_option(boost::asio::serial_port_base::character_size(8));
        port.set_option(boost::asio::serial_port_base::parity(boost::asio::serial_port_base::parity::none));
        port.set_option(boost::asio::serial_port_base::stop_bits(boost::asio::serial_port_base::stop_bits::one));

        auto session = std::make_shared<Session>(nextSessionId_++, std::move(port));
        {
            std::lock_guard<std::mutex> lock(sessionsMutex_);
            sessions_.emplace(session->id(), session);
        }
        session->start(onFrame_, [this](const std::string& error) { notifyError(error); });
        notifyConnected(session);
        return session;
    } catch (const std::exception& e) {
        notifyError(std::string("Serial connect error: ") + e.what());
        return nullptr;
    }
}

void TransportManager::sendToSession(const std::vector<uint8_t>& data, const SessionPtr& session) {
    if (!session) {
        notifyError("Cannot send: session is null");
        return;
    }

    std::lock_guard<std::mutex> lock(sessionsMutex_);
    auto it = sessions_.find(session->id());
    if (it == sessions_.end()) {
        notifyError("Cannot send: session is not active");
        return;
    }

    it->second->send(data, [this](const std::string& error) { notifyError(error); });
}

void TransportManager::disconnectSession(std::uint64_t sessionId) {
    SessionPtr session;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        auto it = sessions_.find(sessionId);
        if (it == sessions_.end()) {
            return;
        }
        session = it->second;
        sessions_.erase(it);
    }

    session->close();
    notifyDisconnected(session);
}

void TransportManager::disconnectAll() {
    std::vector<SessionPtr> sessions;
    {
        std::lock_guard<std::mutex> lock(sessionsMutex_);
        for (auto& [_, session] : sessions_) {
            sessions.push_back(session);
        }
        sessions_.clear();
    }

    for (const auto& session : sessions) {
        session->close();
        notifyDisconnected(session);
    }
}

bool TransportManager::hasActiveConnections() const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    return !sessions_.empty();
}

SessionPtr TransportManager::getFirstConnection() const {
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    if (sessions_.empty()) {
        return nullptr;
    }
    return sessions_.begin()->second;
}

std::vector<SessionPtr> TransportManager::getAllConnections() const {
    std::vector<SessionPtr> result;
    std::lock_guard<std::mutex> lock(sessionsMutex_);
    result.reserve(sessions_.size());
    for (const auto& [_, session] : sessions_) {
        result.push_back(session);
    }
    return result;
}

void TransportManager::setFrameCallback(FrameCallback cb) { onFrame_ = std::move(cb); }
void TransportManager::setConnectionCallback(ConnectionCallback cb) { onConnection_ = std::move(cb); }
void TransportManager::setErrorCallback(ErrorCallback cb) { onError_ = std::move(cb); }

void TransportManager::notifyConnected(const SessionPtr& session) {
    if (onConnection_) {
        onConnection_(true, session);
    }
}

void TransportManager::notifyDisconnected(const SessionPtr& session) {
    if (onConnection_) {
        onConnection_(false, session);
    }
}

void TransportManager::notifyError(const std::string& error) const {
    if (onError_) {
        onError_(error);
    }
}

} // namespace transport
