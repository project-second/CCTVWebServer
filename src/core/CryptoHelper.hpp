#pragma once
#include <string>

class CryptoHelper {
public:
    // Derives a 32-byte key from a secret string using SHA-256
    static std::string deriveKey(const std::string& secret);

    // Encrypts plaintext using AES-256-GCM. Returns hex-encoded representation of: IV (12B) + Tag (16B) + Ciphertext
    static std::string encryptAES256GCM(const std::string& plaintext, const std::string& key);

    // Decrypts hex-encoded (IV + Tag + Ciphertext) using AES-256-GCM
    static std::string decryptAES256GCM(const std::string& hexCiphertext, const std::string& key);

    // Computes SHA-256 hash of plaintext (returns hex string)
    static std::string sha256(const std::string& plaintext);

    // Hashes a password using PBKDF2-HMAC-SHA256 with a salt (returns hex string)
    static std::string hashPasswordPBKDF2(const std::string& password, const std::string& salt);
};
