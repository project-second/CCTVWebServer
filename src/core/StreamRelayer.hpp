#pragma once
#include <string>
#include <thread>
#include <atomic>
#include <mutex>

class StreamRelayer {
public:
    static StreamRelayer& getInstance() {
        static StreamRelayer instance;
        return instance;
    }

    // Starts the ingestion thread for the given RTSP URL
    void start(const std::string& rtspUrl);

    // Stops the ingestion thread and releases resources
    void stop();

    // Returns whether the ingestion pipeline is active
    bool isActive() const;

private:
    StreamRelayer() = default;
    ~StreamRelayer();

    std::atomic<bool> running_{false};
    std::thread thread_;
    std::string rtspUrl_;
    mutable std::mutex mutex_;

    void runLoop();
};
