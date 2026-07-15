#include "../include/Application.hpp"
#include "Database.hpp"
#include <nlohmann/json.hpp>
#include <yaml-cpp/yaml.h>
#include <fstream>
#include <iostream>

void Config::ReadJson(const std::string &path) {
    try {
        std::ifstream f(path);
        if (!f.is_open()) {
            std::cerr << "[Config] Warning: Could not open config file: " << path << ". Using defaults." << std::endl;
            // Set defaults if file not found
            properties_["db_path"] = "vms_server.db";
            properties_["master_secret"] = "VE_SUPER_SECURE_SECRET_KEY_FOR_DATA_ENCRYPTION_2026";
            properties_["server_port"] = "8080";
            
            // Initialize Database with defaults
            Database::getInstance().init(properties_["db_path"], properties_["master_secret"]);
            return;
        }

        nlohmann::json data = nlohmann::json::parse(f);
        for (auto& [key, value] : data.items()) {
            if (value.is_string()) {
                properties_[key] = value.get<std::string>();
            } else if (value.is_number()) {
                properties_[key] = std::to_string(value.get<double>());
            } else if (value.is_boolean()) {
                properties_[key] = value.get<bool>() ? "true" : "false";
            }
        }

        // Initialize Database with values from config
        std::string dbPath = getProperty("db_path");
        std::string masterSecret = getProperty("master_secret");
        if (dbPath.empty()) dbPath = "vms_server.db";
        if (masterSecret.empty()) masterSecret = "VE_SUPER_SECURE_SECRET_KEY_FOR_DATA_ENCRYPTION_2026";

        Database::getInstance().init(dbPath, masterSecret);
        std::cout << "[Config] Configuration parsed and Database initialized." << std::endl;

    } catch (const std::exception& e) {
        std::cerr << "[Config] Parse Error in ReadJson: " << e.what() << ". Falling back to defaults." << std::endl;
        properties_["db_path"] = "vms_server.db";
        properties_["master_secret"] = "VE_SUPER_SECURE_SECRET_KEY_FOR_DATA_ENCRYPTION_2026";
        properties_["server_port"] = "8080";
        Database::getInstance().init(properties_["db_path"], properties_["master_secret"]);
    }
}

void Config::ReadYaml(const std::string &path) {
    try {
        YAML::Node config = YAML::LoadFile(path);
        for (auto const& item : config) {
            std::string key = item.first.as<std::string>();
            std::string value = item.second.as<std::string>();
            properties_[key] = value;
        }
    } catch (const std::exception& e) {
        std::cerr << "[Config] Parse Error in ReadYaml: " << e.what() << std::endl;
    }
}

std::string Config::getProperty(const std::string &key) const {
    auto it = properties_.find(key);
    if (it != properties_.end()) {
        return it->second;
    }
    return "";
}
