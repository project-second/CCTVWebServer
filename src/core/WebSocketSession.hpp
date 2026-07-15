#pragma once
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <memory>
#include <vector>
#include <queue>
#include <mutex>
#include <unordered_set>

namespace beast = boost::beast;
namespace websocket = beast::websocket;
using tcp = boost::asio::ip::tcp;

class WebSocketSession : public std::enable_shared_from_this<WebSocketSession> {
public:
    explicit WebSocketSession(tcp::socket socket);
    ~WebSocketSession();

    void start();
    void sendFrame(const std::vector<uint8_t>& data);

private:
    websocket::stream<beast::tcp_stream> ws_;
    beast::flat_buffer buffer_;
    std::queue<std::vector<uint8_t>> write_queue_;
    std::mutex mutex_;
    bool is_writing_ = false;

    void onAccept(beast::error_code ec);
    void doRead();
    void onRead(beast::error_code ec, std::size_t bytes_transferred);
    void doWrite();
    void onWrite(beast::error_code ec, std::size_t bytes_transferred);
};

class WebSocketHub {
public:
    static WebSocketHub& getInstance() {
        static WebSocketHub instance;
        return instance;
    }

    void registerSession(std::shared_ptr<WebSocketSession> session);
    void unregisterSession(std::shared_ptr<WebSocketSession> session);
    void broadcastFrame(const std::vector<uint8_t>& data);

private:
    WebSocketHub() = default;
    std::mutex mutex_;
    std::unordered_set<std::shared_ptr<WebSocketSession>> sessions_;
};
