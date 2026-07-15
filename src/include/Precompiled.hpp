#pragma once

#include <mutex>
#include <vector>
#include <string>
#include <unordered_map>
#include <iostream>
#include <memory>
#include <exception>
#include <stdexcept>

// Boost Thread Pool wrapper for ServerThreadPool
#include <boost/asio/thread_pool.hpp>

class ServerThreadPool : public boost::asio::thread_pool {
public:
    explicit ServerThreadPool(size_t threads) : boost::asio::thread_pool(threads) {}
};
