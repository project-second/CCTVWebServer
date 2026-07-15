#include <functional>
#include <memory>
#include <mutex>
#include <vector>
#include <string>
#include <thread>
#include <unordered_map>
#include <iostream>
#include <boost/asio/thread_pool.hpp>

class ServerThreadPool : public boost::asio::thread_pool {
public:
    explicit ServerThreadPool(size_t threads) : boost::asio::thread_pool(threads) {}
};

class EventBus {
public:
  static EventBus &getInstance() {
    static EventBus instance;
    return instance;
  }

  template <typename EventT>
  using Handler = std::function<void(const EventT &)>;

  template <typename EventT> void subscribe(Handler<EventT> handler) {
    std::lock_guard<std::mutex> lock(mutex_);
    handlers<EventT>().push_back(std::move(handler));
  }

  template <typename EventT> void publish(const EventT &event) {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto &h : handlers<EventT>())
      h(event);
  }

private:
  EventBus() = default;
  std::mutex mutex_;

  template <typename EventT> std::vector<Handler<EventT>> &handlers() {
    static std::vector<Handler<EventT>> instance;
    return instance;
  }
};

class Config {
public:
  static Config &getInstance() {
    static Config instance;
    return instance;
  }

  void ReadJson(const std::string &path);
  void ReadYaml(const std::string &path);
  std::string getProperty(const std::string &key) const;

private:
  Config() = default;
  std::unordered_map<std::string, std::string> properties_;
};

class DBThreadPool {
public:
  explicit DBThreadPool(size_t size = 2) {}
};

class DBConnector {
public:
  // pool을 참조가 아닌 shared_ptr로 받아 lifetime을 명확히 함
  explicit DBConnector(std::shared_ptr<DBThreadPool> pool = nullptr)
      : pool_(std::move(pool)) {}

  void SetThreadPool(std::shared_ptr<DBThreadPool> pool) {
    pool_ = std::move(pool);
  }

private:
  std::shared_ptr<DBThreadPool> pool_;
};

class DBController {
public:
  DBController()
      : dbThreadPool_(std::make_shared<DBThreadPool>(2)),
        dbConnector_(dbThreadPool_), eventBus_(EventBus::getInstance()) {}

private:
  std::shared_ptr<DBThreadPool> dbThreadPool_;
  DBConnector dbConnector_;
  EventBus &eventBus_;
};

struct HttpRequestEvent {
  int connId;
  std::string method, url, body;
  std::string cookies;
};

struct HttpResponseEvent {
  int connId;
  int statusCode;
  std::string body;
};

class Connection {
public:
  explicit Connection(int fd) : fd_(fd) {}
  int fd() const { return fd_; }
  // 버퍼, keep-alive 여부 등은 추후 채움

private:
  int fd_;
};

class HttpServer {
public:
  explicit HttpServer(std::shared_ptr<ServerThreadPool> pool);

  void onRead(); // accept/read 루프, 파싱 후
                 // eventBus_.publish(HttpRequestEvent{...})

private:
  void writeResponse(const HttpResponseEvent &e);

  std::shared_ptr<ServerThreadPool> pool_;
  EventBus &eventBus_;
  std::unordered_map<int, Connection> connections_;
};

class ReplyServer {
public:
  using Handler = std::function<HttpResponseEvent(const HttpRequestEvent &)>;

  ReplyServer() : eventBus_(EventBus::getInstance()) {
    eventBus_.subscribe<HttpRequestEvent>(
        [this](const HttpRequestEvent &e) { dispatch(e); });
  }

  void AddReplyer(std::string url, Handler handler) {
    handlers_[std::move(url)] = std::move(handler);
  }

private:
  void dispatch(const HttpRequestEvent &e);

  EventBus &eventBus_;
  std::unordered_map<std::string, Handler> handlers_;
};

class WebServerController {
public:
  WebServerController()
      : serverThreadPool_(std::make_shared<ServerThreadPool>(4)),
        httpServer_(serverThreadPool_), replyServer_(),
        eventBus_(EventBus::getInstance()) {
    replyServer_.AddReplyer("/health", [](const HttpRequestEvent &) {
      return HttpResponseEvent{0, 200, "OK"};
    });
  }

private:
  std::shared_ptr<ServerThreadPool> serverThreadPool_;
  HttpServer httpServer_;
  ReplyServer replyServer_;
  EventBus &eventBus_;
};

// ===== Application: 예외를 삼키지 않고 로깅 =====
class Application {
public:
  Application() { Config::getInstance().ReadJson("config.json"); }

  void run() {
    try {
      dbController_ = std::make_unique<DBController>();
      webServerController_ = std::make_unique<WebServerController>();
      
      // Block the main thread to keep the service running
      while (true) {
          std::this_thread::sleep_for(std::chrono::seconds(1));
      }
    } catch (const std::exception &ec) {
      std::cerr << "[Application] init failed: " << ec.what() << std::endl;
      throw;
    }
  }

private:
  std::unique_ptr<DBController> dbController_;
  std::unique_ptr<WebServerController> webServerController_;
};