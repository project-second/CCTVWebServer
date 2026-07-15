#include "./include/Application.hpp"
#include <boost/asio.hpp>
#include <boost/beast.hpp>
#include <boost/json.hpp>
#include <iostream>
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>

int main() {
  Application App_;
  App_.run();
  return 0;
}
