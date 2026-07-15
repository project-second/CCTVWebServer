#include "../include/Application.hpp"
#include "Database.hpp"
#include "CryptoHelper.hpp"
#include "StreamRelayer.hpp"
#include <nlohmann/json.hpp>
#include <fstream>
#include <sstream>
#include <iostream>
#include <random>
#include <mutex>
#include <unordered_map>

// Global Session Map
static std::unordered_map<std::string, std::string> g_activeSessions; // token -> user_id
static std::mutex g_sessionsMutex;

namespace {
    std::string generateSessionToken() {
        static std::random_device rd;
        static std::mt19937 gen(rd());
        static std::uniform_int_distribution<> dis(0, 15);
        std::stringstream ss;
        for (int i = 0; i < 32; ++i) {
            ss << std::hex << dis(gen);
        }
        return ss.str();
    }

    std::string getSessionToken(const std::string& cookieHeader) {
        size_t pos = cookieHeader.find("SessionID=");
        if (pos == std::string::npos) return "";
        pos += 10;
        size_t endPos = cookieHeader.find(';', pos);
        if (endPos == std::string::npos) {
            return cookieHeader.substr(pos);
        }
        return cookieHeader.substr(pos, endPos - pos);
    }

    std::string getUserIdFromSession(const std::string& cookieHeader) {
        std::string token = getSessionToken(cookieHeader);
        if (token.empty()) return "";
        std::lock_guard<std::mutex> lock(g_sessionsMutex);
        auto it = g_activeSessions.find(token);
        if (it != g_activeSessions.end()) {
            return it->second;
        }
        return "";
    }

    std::string readStaticFile(const std::string& relPath) {
        std::string fullPath = "src/www" + relPath;
        if (relPath == "/" || relPath.empty()) {
            fullPath = "src/www/index.html";
        }
        std::ifstream f(fullPath, std::ios::binary);
        if (!f.is_open()) return "";
        std::stringstream ss;
        ss << f.rdbuf();
        return ss.str();
    }

    std::string makeHttpResponse(int statusCode, const std::string& statusText, const std::string& contentType, const std::string& body, const std::string& extraHeaders = "") {
        std::stringstream ss;
        ss << "HTTP/1.1 " << statusCode << " " << statusText << "\r\n"
           << "Content-Type: " << contentType << "\r\n"
           << "Content-Length: " << body.size() << "\r\n"
           << "Connection: close\r\n"
           << extraHeaders
           << "\r\n"
           << body;
        return ss.str();
    }
}

void ReplyServer::dispatch(const HttpRequestEvent &e) {
    // -------------------------------------------------------------
    // 1. Static Files serving
    // -------------------------------------------------------------
    if (e.url == "/" || e.url == "/index.html" || e.url == "/style.css" || e.url == "/app.js") {
        std::string content = readStaticFile(e.url);
        if (!content.empty()) {
            std::string mime = "text/html";
            if (e.url.find(".css") != std::string::npos) mime = "text/css";
            else if (e.url.find(".js") != std::string::npos) mime = "application/javascript";

            std::string resp = makeHttpResponse(200, "OK", mime, content);
            eventBus_.publish(HttpResponseEvent{e.connId, 200, resp});
            return;
        }
    }

    // -------------------------------------------------------------
    // 2. REST API Endpoints
    // -------------------------------------------------------------
    try {
        // --- PUBLIC: REGISTER ---
        if (e.url == "/api/register" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string id = json.value("id", "");
            std::string username = json.value("username", "");
            std::string password = json.value("password", "");

            if (id.empty() || username.empty() || password.empty()) {
                std::string body = nlohmann::json{{"error", "Missing required fields"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 400, makeHttpResponse(400, "Bad Request", "application/json", body)});
                return;
            }

            if (Database::getInstance().registerUser(id, username, password)) {
                std::string body = nlohmann::json{{"message", "Registration pending approval!"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            } else {
                std::string body = nlohmann::json{{"error", "Registration failed. ID is already taken or banned."}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 400, makeHttpResponse(400, "Bad Request", "application/json", body)});
            }
            return;
        }

        // --- PUBLIC: LOGIN ---
        if (e.url == "/api/login" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string id = json.value("id", "");
            std::string password = json.value("password", "");

            UserRecord user;
            if (!Database::getInstance().getUser(id, user) || user.isActive == 0) {
                std::string body = nlohmann::json{{"error", "Invalid ID or password"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 401, makeHttpResponse(401, "Unauthorized", "application/json", body)});
                return;
            }

            if (user.isApproved == 0) {
                std::string body = nlohmann::json{{"error", "Account pending administrator approval"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 401, makeHttpResponse(401, "Unauthorized", "application/json", body)});
                return;
            }

            std::string calculatedHash = CryptoHelper::hashPasswordPBKDF2(password, id);
            if (calculatedHash != user.passwordHash) {
                std::string body = nlohmann::json{{"error", "Invalid ID or password"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 401, makeHttpResponse(401, "Unauthorized", "application/json", body)});
                return;
            }

            // Create session
            std::string token = generateSessionToken();
            {
                std::lock_guard<std::mutex> lock(g_sessionsMutex);
                g_activeSessions[token] = id;
            }

            std::string body = nlohmann::json{{"message", "Success"}}.dump();
            std::string extraHeaders = "Set-Cookie: SessionID=" + token + "; Path=/; HttpOnly\r\n";
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body, extraHeaders)});
            return;
        }

        // --- AUTHENTICATED API CHECKS ---
        std::string callerId = getUserIdFromSession(e.cookies);
        if (callerId.empty()) {
            // Endpoints starting with /api/ require authentication
            if (e.url.rfind("/api/", 0) == 0) {
                std::string body = nlohmann::json{{"error", "Unauthorized"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 401, makeHttpResponse(401, "Unauthorized", "application/json", body)});
                return;
            }
        }

        UserRecord callerUser;
        Database::getInstance().getUser(callerId, callerUser);

        // --- AUTH: LOGOUT ---
        if (e.url == "/api/logout" && e.method == "POST") {
            std::string token = getSessionToken(e.cookies);
            if (!token.empty()) {
                std::lock_guard<std::mutex> lock(g_sessionsMutex);
                g_activeSessions.erase(token);
            }
            std::string body = nlohmann::json{{"message", "Success"}}.dump();
            std::string extraHeaders = "Set-Cookie: SessionID=; Path=/; Expires=Thu, 01 Jan 1970 00:00:00 GMT\r\n";
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body, extraHeaders)});
            return;
        }

        // --- AUTH: ME PROFILE ---
        if (e.url == "/api/user/me" && e.method == "GET") {
            std::string body = nlohmann::json{
                {"user_id", callerUser.userId},
                {"username", callerUser.username},
                {"role", callerUser.role}
            }.dump();
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            return;
        }

        // --- AUTH: PROFILE UPDATE ---
        if (e.url == "/api/user/update" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string username = json.value("username", "");
            std::string password = json.value("password", "");

            if (username.empty()) {
                std::string body = nlohmann::json{{"error", "Username cannot be empty"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 400, makeHttpResponse(400, "Bad Request", "application/json", body)});
                return;
            }

            if (Database::getInstance().updateUserProfile(callerId, username, password)) {
                std::string body = nlohmann::json{{"message", "Success"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            } else {
                std::string body = nlohmann::json{{"error", "Update failed"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 500, makeHttpResponse(500, "Internal Error", "application/json", body)});
            }
            return;
        }

        // --- ADMIN ONLY ENDPOINTS ---
        if (callerUser.role != "admin") {
            if (e.url.rfind("/api/users", 0) == 0 || e.url.rfind("/api/rtsp", 0) == 0) {
                std::string body = nlohmann::json{{"error", "Forbidden"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 403, makeHttpResponse(403, "Forbidden", "application/json", body)});
                return;
            }
        }

        // --- ADMIN: LIST USERS ---
        if (e.url == "/api/users" && e.method == "GET") {
            auto users = Database::getInstance().getAllUsers();
            auto arr = nlohmann::json::array();
            for (auto& u : users) {
                arr.push_back({
                    {"user_id", u.userId},
                    {"username", u.username},
                    {"role", u.role},
                    {"is_approved", u.isApproved},
                    {"is_active", u.isActive}
                });
            }
            std::string body = arr.dump();
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            return;
        }

        // --- ADMIN: APPROVE USER ---
        if (e.url == "/api/users/approve" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string targetId = json.value("id", "");
            
            if (Database::getInstance().approveUser(targetId)) {
                std::string body = nlohmann::json{{"message", "Success"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            } else {
                std::string body = nlohmann::json{{"error", "Approval failed"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 500, makeHttpResponse(500, "Internal Error", "application/json", body)});
            }
            return;
        }

        // --- ADMIN: EVICT USER ---
        if (e.url == "/api/users/evict" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string targetId = json.value("id", "");

            if (targetId == "admin") {
                std::string body = nlohmann::json{{"error", "Cannot ban the root admin"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 400, makeHttpResponse(400, "Bad Request", "application/json", body)});
                return;
            }

            if (Database::getInstance().evictUser(targetId)) {
                // Evict session token from active sessions immediately
                {
                    std::lock_guard<std::mutex> lock(g_sessionsMutex);
                    for (auto it = g_activeSessions.begin(); it != g_activeSessions.end();) {
                        if (it->second == targetId) {
                            it = g_activeSessions.erase(it);
                        } else {
                            ++it;
                        }
                    }
                }

                std::string body = nlohmann::json{{"message", "Success"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            } else {
                std::string body = nlohmann::json{{"error", "Eviction failed"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 500, makeHttpResponse(500, "Internal Error", "application/json", body)});
            }
            return;
        }

        // --- ADMIN: GET RTSP CONFIG ---
        if (e.url == "/api/rtsp" && e.method == "GET") {
            RtspRecord config;
            auto arr = nlohmann::json::array();
            if (Database::getInstance().getLatestRtspConfig(config)) {
                arr.push_back({
                    {"name", config.name},
                    {"url", config.url}
                });
            }
            std::string body = arr.dump();
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            return;
        }

        // --- ADMIN: SAVE & START RTSP ---
        if (e.url == "/api/rtsp" && e.method == "POST") {
            auto json = nlohmann::json::parse(e.body);
            std::string name = json.value("name", "");
            std::string url = json.value("url", "");

            if (name.empty() || url.empty()) {
                std::string body = nlohmann::json{{"error", "Missing fields"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 400, makeHttpResponse(400, "Bad Request", "application/json", body)});
                return;
            }

            Database::getInstance().clearRtspConfigs(); // keep only latest
            if (Database::getInstance().saveRtspConfig(name, url)) {
                StreamRelayer::getInstance().start(url);
                std::string body = nlohmann::json{{"message", "Stream started successfully"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            } else {
                std::string body = nlohmann::json{{"error", "Failed to save config"}}.dump();
                eventBus_.publish(HttpResponseEvent{e.connId, 500, makeHttpResponse(500, "Internal Error", "application/json", body)});
            }
            return;
        }

        // --- ADMIN: STOP RTSP ---
        if (e.url == "/api/rtsp/stop" && e.method == "POST") {
            StreamRelayer::getInstance().stop();
            std::string body = nlohmann::json{{"message", "Stream stopped successfully"}}.dump();
            eventBus_.publish(HttpResponseEvent{e.connId, 200, makeHttpResponse(200, "OK", "application/json", body)});
            return;
        }

    } catch (const std::exception& ex) {
        std::cerr << "[ReplyServer] Exception in dispatch: " << ex.what() << std::endl;
        std::string body = nlohmann::json{{"error", std::string(ex.what())}}.dump();
        eventBus_.publish(HttpResponseEvent{e.connId, 500, makeHttpResponse(500, "Internal Server Error", "application/json", body)});
        return;
    }

    // -------------------------------------------------------------
    // 3. Fallback to registered handlers (like /health)
    // -------------------------------------------------------------
    auto it = handlers_.find(e.url);
    if (it != handlers_.end()) {
        eventBus_.publish(it->second(e));
        return;
    }

    // Default 404 Fallback
    std::string body = nlohmann::json{{"error", "Not Found"}}.dump();
    eventBus_.publish(HttpResponseEvent{e.connId, 404, makeHttpResponse(404, "Not Found", "application/json", body)});
}
