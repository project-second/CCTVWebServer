#include "../include/Application.hpp"
#include "WebSocketSession.hpp"
#include <boost/asio/ip/tcp.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/core.hpp>
#include <iostream>
#include <memory>
#include <sstream>
#include <mutex>
#include <atomic>

#ifdef _WIN32
#include <winsock2.h>
#else
#include <unistd.h>
#include <sys/socket.h>
#endif

using tcp = boost::asio::ip::tcp;
namespace http = boost::beast::http;

static std::atomic<int> g_nextConnId{1};
static std::mutex g_connectionsMutex;

class HttpSession : public std::enable_shared_from_this<HttpSession> {
public:
    HttpSession(tcp::socket socket, int connId, HttpServer* server)
        : socket_(std::move(socket)), connId_(connId), server_(server) {}

    void start() {
        http::async_read(
            socket_,
            buffer_,
            req_,
            beast::bind_front_handler(
                &HttpSession::onRead,
                shared_from_this()));
    }

private:
    tcp::socket socket_;
    beast::flat_buffer buffer_;
    http::request<http::string_body> req_;
    int connId_;
    HttpServer* server_;

    void onRead(beast::error_code ec, std::size_t bytes_transferred) {
        boost::ignore_unused(bytes_transferred);

        if (ec) {
            // Silent drop on connection reset or closed
            return;
        }

        // Check if this is a WebSocket Upgrade request
        if (websocket::is_upgrade(req_)) {
            // Hand over the socket directly to the WebSocketSession
            auto wsSession = std::make_shared<WebSocketSession>(std::move(socket_));
            wsSession->start();
            return;
        }

        // Normal REST API / Static File HTTP request
        int fd = socket_.release(); // release native descriptor ownership

        {
            std::lock_guard<std::mutex> lock(g_connectionsMutex);
            server_->connections_.emplace(connId_, Connection(fd));
        }

        // Publish to EventBus
        std::string method = std::string(req_.method_string());
        std::string target = std::string(req_.target());
        std::string body = req_.body();
        std::string cookies = std::string(req_[http::field::cookie]);

        HttpRequestEvent reqEvent{ connId_, method, target, body, cookies };
        EventBus::getInstance().publish(reqEvent);
    }
};

// ------------------------------------------------------------------
// HttpServer Implementation
// ------------------------------------------------------------------

HttpServer::HttpServer(std::shared_ptr<ServerThreadPool> pool)
    : pool_(std::move(pool)), eventBus_(EventBus::getInstance()) {
    eventBus_.subscribe<HttpResponseEvent>(
        [this](const HttpResponseEvent &e) { writeResponse(e); });
    
    // Spawn accept loop in thread pool
    boost::asio::post(*pool_, [this]() { onRead(); });
}

void HttpServer::onRead() {
    try {
        std::string portStr = Config::getInstance().getProperty("server_port");
        int port = portStr.empty() ? 8080 : std::stoi(portStr);

        auto executor = pool_->get_executor();
        auto acceptor = std::make_shared<tcp::acceptor>(executor, tcp::endpoint(tcp::v4(), port));
        std::cout << "[HttpServer] Listening on port " << port << "..." << std::endl;

        // Recursive lambda-like class to run loop
        struct AcceptLoop {
            std::shared_ptr<tcp::acceptor> acceptor;
            HttpServer* server;

            void run() {
                acceptor->async_accept(
                    [this, self = *this](beast::error_code ec, tcp::socket socket) mutable {
                        if (!ec) {
                            int connId = g_nextConnId++;
                            auto session = std::make_shared<HttpSession>(std::move(socket), connId, server);
                            session->start();
                        }
                        run(); // accept next connection
                    });
            }
        };

        AcceptLoop{acceptor, this}.run();

    } catch (const std::exception& e) {
        std::cerr << "[HttpServer] Critical error in accept loop: " << e.what() << std::endl;
    }
}

void HttpServer::writeResponse(const HttpResponseEvent &e) {
    int fd = -1;
    {
        std::lock_guard<std::mutex> lock(g_connectionsMutex);
        auto it = connections_.find(e.connId);
        if (it == connections_.end()) {
            return;
        }
        fd = it->second.fd();
        connections_.erase(it);
    }

    if (fd != -1) {
        std::string response = e.body;

        // If the reply handlers didn't write full HTTP headers, wrap in a standard HTTP envelope
        if (response.rfind("HTTP/1.1", 0) != 0) {
            std::string statusText = "200 OK";
            if (e.statusCode == 404) statusText = "404 Not Found";
            else if (e.statusCode == 400) statusText = "400 Bad Request";
            else if (e.statusCode == 401) statusText = "401 Unauthorized";
            else if (e.statusCode == 403) statusText = "403 Forbidden";
            else if (e.statusCode == 500) statusText = "500 Internal Server Error";

            std::string contentType = "application/json";
            if (response.find("<!DOCTYPE html>") != std::string::npos || response.find("<html>") != std::string::npos) {
                contentType = "text/html";
            }

            std::stringstream ss;
            ss << "HTTP/1.1 " << e.statusCode << " " << statusText << "\r\n"
               << "Content-Type: " << contentType << "\r\n"
               << "Content-Length: " << response.size() << "\r\n"
               << "Connection: close\r\n\r\n"
               << response;
            response = ss.str();
        }

        // Perform synchronous socket write & close
#ifdef _WIN32
        send(fd, response.data(), (int)response.size(), 0);
        closesocket(fd);
#else
        if (write(fd, response.data(), response.size()) < 0) {
            // Handle socket write failure silently
        }
        close(fd);
#endif
    }
}
