#include "Database.hpp"
#include "CryptoHelper.hpp"
#include <iostream>
#include <sstream>
#include <stdexcept>

Database::~Database() {
    if (db_) {
        sqlite3_close(db_);
        db_ = nullptr;
    }
}

void Database::init(const std::string& dbPath, const std::string& masterSecret) {
    if (sqlite3_open(dbPath.c_str(), &db_) != SQLITE_OK) {
        throw std::runtime_error("Failed to open sqlite3 database: " + std::string(sqlite3_errmsg(db_)));
    }

    // Derive 32-byte key for AES-256-GCM
    key_ = CryptoHelper::deriveKey(masterSecret);

    // Create Tables
    std::string createUsersTable = 
        "CREATE TABLE IF NOT EXISTS users ("
        "  user_id_hash TEXT PRIMARY KEY,"
        "  user_id_enc TEXT NOT NULL,"
        "  username_enc TEXT NOT NULL,"
        "  password_hash TEXT NOT NULL,"
        "  role TEXT NOT NULL,"
        "  is_approved INTEGER NOT NULL DEFAULT 0,"
        "  is_active INTEGER NOT NULL DEFAULT 1"
        ");";

    std::string createRtspTable =
        "CREATE TABLE IF NOT EXISTS rtsp_config ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name_enc TEXT NOT NULL,"
        "  url_enc TEXT NOT NULL,"
        "  created_at DATETIME DEFAULT CURRENT_TIMESTAMP"
        ");";

    executeQuery(createUsersTable);
    executeQuery(createRtspTable);

    // Bootstrap Admin User
    bootstrapAdmin();
}

void Database::executeQuery(const std::string& sql) {
    char* errMsg = nullptr;
    if (sqlite3_exec(db_, sql.c_str(), nullptr, nullptr, &errMsg) != SQLITE_OK) {
        std::string errStr = errMsg ? errMsg : "unknown error";
        sqlite3_free(errMsg);
        throw std::runtime_error("SQL Exec Error: " + errStr + " (Query: " + sql + ")");
    }
}

void Database::bootstrapAdmin() {
    std::string adminId = "admin";
    std::string adminIdHash = CryptoHelper::sha256(adminId);

    // Check if admin exists
    std::string sql = "SELECT COUNT(*) FROM users WHERE user_id_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return;
    }
    sqlite3_bind_text(stmt, 1, adminIdHash.c_str(), -1, SQLITE_STATIC);
    
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (count == 0) {
        // Create Admin
        std::string adminPw = "admin";
        std::string adminPwHash = CryptoHelper::hashPasswordPBKDF2(adminPw, adminId);
        std::string adminUsername = "Administrator";

        std::string encId = CryptoHelper::encryptAES256GCM(adminId, key_);
        std::string encUsername = CryptoHelper::encryptAES256GCM(adminUsername, key_);

        std::string insertSql = 
            "INSERT INTO users (user_id_hash, user_id_enc, username_enc, password_hash, role, is_approved, is_active) "
            "VALUES (?, ?, ?, ?, 'admin', 1, 1);";

        sqlite3_stmt* instmt = nullptr;
        if (sqlite3_prepare_v2(db_, insertSql.c_str(), -1, &instmt, nullptr) == SQLITE_OK) {
            sqlite3_bind_text(instmt, 1, adminIdHash.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(instmt, 2, encId.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(instmt, 3, encUsername.c_str(), -1, SQLITE_STATIC);
            sqlite3_bind_text(instmt, 4, adminPwHash.c_str(), -1, SQLITE_STATIC);
            sqlite3_step(instmt);
            sqlite3_finalize(instmt);
            std::cout << "[Database] Successfully bootstrapped admin account." << std::endl;
        }
    }
}

bool Database::registerUser(const std::string& userId, const std::string& username, const std::string& password) {
    std::string idHash = CryptoHelper::sha256(userId);

    // Check if user exists (active or banned)
    std::string checkSql = "SELECT COUNT(*) FROM users WHERE user_id_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, checkSql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, idHash.c_str(), -1, SQLITE_STATIC);
    int count = 0;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        count = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (count > 0) {
        // ID already exists (either active, pending, or evicted/banned)
        return false;
    }

    // Encrypt columns & hash password
    std::string encId = CryptoHelper::encryptAES256GCM(userId, key_);
    std::string encUsername = CryptoHelper::encryptAES256GCM(username, key_);
    std::string pwHash = CryptoHelper::hashPasswordPBKDF2(password, userId);

    std::string insertSql = 
        "INSERT INTO users (user_id_hash, user_id_enc, username_enc, password_hash, role, is_approved, is_active) "
        "VALUES (?, ?, ?, ?, 'user', 0, 1);";

    sqlite3_stmt* instmt = nullptr;
    if (sqlite3_prepare_v2(db_, insertSql.c_str(), -1, &instmt, nullptr) != SQLITE_OK) {
        return false;
    }

    sqlite3_bind_text(instmt, 1, idHash.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(instmt, 2, encId.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(instmt, 3, encUsername.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(instmt, 4, pwHash.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(instmt);
    sqlite3_finalize(instmt);

    return (rc == SQLITE_DONE);
}

bool Database::approveUser(const std::string& userId) {
    std::string idHash = CryptoHelper::sha256(userId);
    std::string sql = "UPDATE users SET is_approved = 1 WHERE user_id_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, idHash.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::evictUser(const std::string& userId) {
    std::string idHash = CryptoHelper::sha256(userId);
    std::string sql = "UPDATE users SET is_active = 0 WHERE user_id_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, idHash.c_str(), -1, SQLITE_STATIC);
    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::updateUserProfile(const std::string& userId, const std::string& newUsername, const std::string& newPassword) {
    std::string idHash = CryptoHelper::sha256(userId);

    std::string sql = "UPDATE users SET username_enc = ?";
    if (!newPassword.empty()) {
        sql += ", password_hash = ?";
    }
    sql += " WHERE user_id_hash = ?;";

    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string encUsername = CryptoHelper::encryptAES256GCM(newUsername, key_);
    std::string pwHash;

    sqlite3_bind_text(stmt, 1, encUsername.c_str(), -1, SQLITE_STATIC);
    if (!newPassword.empty()) {
        pwHash = CryptoHelper::hashPasswordPBKDF2(newPassword, userId);
        sqlite3_bind_text(stmt, 2, pwHash.c_str(), -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, idHash.c_str(), -1, SQLITE_STATIC);
    } else {
        sqlite3_bind_text(stmt, 2, idHash.c_str(), -1, SQLITE_STATIC);
    }

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::getUser(const std::string& userId, UserRecord& outUser) {
    std::string idHash = CryptoHelper::sha256(userId);
    std::string sql = "SELECT user_id_enc, username_enc, password_hash, role, is_approved, is_active FROM users WHERE user_id_hash = ?;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }
    sqlite3_bind_text(stmt, 1, idHash.c_str(), -1, SQLITE_STATIC);

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        std::string encId = (const char*)sqlite3_column_text(stmt, 0);
        std::string encUsername = (const char*)sqlite3_column_text(stmt, 1);
        
        try {
            outUser.userId = CryptoHelper::decryptAES256GCM(encId, key_);
            outUser.username = CryptoHelper::decryptAES256GCM(encUsername, key_);
        } catch (const std::exception& e) {
            std::cerr << "[Database] Decryption error in getUser: " << e.what() << std::endl;
            sqlite3_finalize(stmt);
            return false;
        }

        outUser.passwordHash = (const char*)sqlite3_column_text(stmt, 2);
        outUser.role = (const char*)sqlite3_column_text(stmt, 3);
        outUser.isApproved = sqlite3_column_int(stmt, 4);
        outUser.isActive = sqlite3_column_int(stmt, 5);
    }
    sqlite3_finalize(stmt);
    return found;
}

std::vector<UserRecord> Database::getAllUsers() {
    std::vector<UserRecord> users;
    std::string sql = "SELECT user_id_enc, username_enc, password_hash, role, is_approved, is_active FROM users;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return users;
    }

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        UserRecord rec;
        std::string encId = (const char*)sqlite3_column_text(stmt, 0);
        std::string encUsername = (const char*)sqlite3_column_text(stmt, 1);

        try {
            rec.userId = CryptoHelper::decryptAES256GCM(encId, key_);
            rec.username = CryptoHelper::decryptAES256GCM(encUsername, key_);
            rec.passwordHash = (const char*)sqlite3_column_text(stmt, 2);
            rec.role = (const char*)sqlite3_column_text(stmt, 3);
            rec.isApproved = sqlite3_column_int(stmt, 4);
            rec.isActive = sqlite3_column_int(stmt, 5);
            users.push_back(rec);
        } catch (const std::exception& e) {
            std::cerr << "[Database] Decryption error in getAllUsers: " << e.what() << std::endl;
        }
    }
    sqlite3_finalize(stmt);
    return users;
}

bool Database::saveRtspConfig(const std::string& name, const std::string& url) {
    std::string sql = "INSERT INTO rtsp_config (name_enc, url_enc) VALUES (?, ?);";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    std::string encName = CryptoHelper::encryptAES256GCM(name, key_);
    std::string encUrl = CryptoHelper::encryptAES256GCM(url, key_);

    sqlite3_bind_text(stmt, 1, encName.c_str(), -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, encUrl.c_str(), -1, SQLITE_STATIC);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return (rc == SQLITE_DONE);
}

bool Database::getLatestRtspConfig(RtspRecord& outRtsp) {
    std::string sql = "SELECT id, name_enc, url_enc FROM rtsp_config ORDER BY id DESC LIMIT 1;";
    sqlite3_stmt* stmt = nullptr;
    if (sqlite3_prepare_v2(db_, sql.c_str(), -1, &stmt, nullptr) != SQLITE_OK) {
        return false;
    }

    bool found = false;
    if (sqlite3_step(stmt) == SQLITE_ROW) {
        found = true;
        outRtsp.id = sqlite3_column_int(stmt, 0);
        std::string encName = (const char*)sqlite3_column_text(stmt, 1);
        std::string encUrl = (const char*)sqlite3_column_text(stmt, 2);

        try {
            outRtsp.name = CryptoHelper::decryptAES256GCM(encName, key_);
            outRtsp.url = CryptoHelper::decryptAES256GCM(encUrl, key_);
        } catch (const std::exception& e) {
            std::cerr << "[Database] Decryption error in getLatestRtspConfig: " << e.what() << std::endl;
            found = false;
        }
    }
    sqlite3_finalize(stmt);
    return found;
}

bool Database::clearRtspConfigs() {
    try {
        executeQuery("DELETE FROM rtsp_config;");
        return true;
    } catch (...) {
        return false;
    }
}
