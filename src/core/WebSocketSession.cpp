#include "WebSocketSession.hpp"
#include <iostream>

WebSocketSession::WebSocketSession(tcp::socket socket)
    : ws_(std::move(socket)) {
    // Set suggested timeout settings for the websocket
    ws_.set_option(websocket::stream_base::timeout::suggested(beast::role_type::server));
}

WebSocketSession::~WebSocketSession() {
    WebSocketHub::getInstance().unregisterSession(shared_from_this());
}

void WebSocketSession::start() {
    // Set binary mode
    ws_.binary(true);

    // Accept the websocket handshake
    ws_.async_accept(
        beast::bind_front_handler(
            &WebSocketSession::onAccept,
            shared_from_this()));
}

void WebSocketSession::onAccept(beast::error_code ec) {
    if (ec) {
        std::cerr << "[WebSocket] Accept error: " << ec.message() << std::endl;
        return;
    }

    // Register session in the hub
    WebSocketHub::getInstance().registerSession(shared_from_this());
    std::cout << "[WebSocket] Client connected. Active sessions: " << std::endl;

    // Start reading control messages (like pings or close requests)
    doRead();
}

void WebSocketSession::doRead() {
    ws_.async_read(
        buffer_,
        beast::bind_front_handler(
            &WebSocketSession::onRead,
            shared_from_this()));
}

void WebSocketSession::onRead(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec == websocket::error::closed || ec == beast::errc::connection_reset) {
        std::cout << "[WebSocket] Client disconnected." << std::endl;
        WebSocketHub::getInstance().unregisterSession(shared_from_this());
        return;
    } else if (ec) {
        std::cerr << "[WebSocket] Read error: " << ec.message() << std::endl;
        WebSocketHub::getInstance().unregisterSession(shared_from_this());
        return;
    }

    buffer_.consume(buffer_.size());
    doRead();
}

void WebSocketSession::sendFrame(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    write_queue_.push(data);
    if (!is_writing_) {
        is_writing_ = true;
        doWrite();
    }
}

void WebSocketSession::doWrite() {
    // Assumes mutex_ is locked by caller if checking write_queue_, but we'll lock internally for async safety
    // To avoid deadlocks we copy the front element
    std::vector<uint8_t> frame;
    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (write_queue_.empty()) {
            is_writing_ = false;
            return;
        }
        frame = write_queue_.front();
    }

    ws_.async_write(
        boost::asio::buffer(frame),
        beast::bind_front_handler(
            &WebSocketSession::onWrite,
            shared_from_this()));
}

void WebSocketSession::onWrite(beast::error_code ec, std::size_t bytes_transferred) {
    boost::ignore_unused(bytes_transferred);

    if (ec) {
        std::cerr << "[WebSocket] Write error: " << ec.message() << std::endl;
        WebSocketHub::getInstance().unregisterSession(shared_from_this());
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        if (!write_queue_.empty()) {
            write_queue_.pop();
        }
    }

    doWrite();
}

// ------------------------------------------------------------------
// WebSocketHub Implementation
// ------------------------------------------------------------------

void WebSocketHub::registerSession(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.insert(session);
}

void WebSocketHub::unregisterSession(std::shared_ptr<WebSocketSession> session) {
    std::lock_guard<std::mutex> lock(mutex_);
    sessions_.erase(session);
}

void WebSocketHub::broadcastFrame(const std::vector<uint8_t>& data) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& session : sessions_) {
        session->sendFrame(data);
    }
}
