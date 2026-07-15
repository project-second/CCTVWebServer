#pragma once
#include <string>
#include <vector>
#include <sqlite3.h>

struct UserRecord {
    std::string userId;
    std::string username;
    std::string passwordHash;
    std::string role;
    int isApproved;
    int isActive;
};

struct RtspRecord {
    int id;
    std::string name;
    std::string url;
};

class Database {
public:
    static Database& getInstance() {
        static Database instance;
        return instance;
    }

    // Initialize database connection, schemas, and master encryption key
    void init(const std::string& dbPath, const std::string& masterSecret);

    // User authentication and registration
    bool registerUser(const std::string& userId, const std::string& username, const std::string& password);
    bool approveUser(const std::string& userId);
    bool evictUser(const std::string& userId);
    bool updateUserProfile(const std::string& userId, const std::string& newUsername, const std::string& newPassword = "");
    
    bool getUser(const std::string& userId, UserRecord& outUser);
    std::vector<UserRecord> getAllUsers();

    // RTSP Stream configs
    bool saveRtspConfig(const std::string& name, const std::string& url);
    bool getLatestRtspConfig(RtspRecord& outRtsp);
    bool clearRtspConfigs();

private:
    Database() = default;
    ~Database();

    sqlite3* db_ = nullptr;
    std::string key_; // 32-byte derived AES key for GCM

    void executeQuery(const std::string& sql);
    void bootstrapAdmin();
};
