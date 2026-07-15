#include "CryptoHelper.hpp"
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/err.h>
#include <sstream>
#include <iomanip>
#include <cstring>
#include <stdexcept>
#include <vector>

namespace {
    std::string toHex(const unsigned char* data, size_t len) {
        std::stringstream ss;
        ss << std::hex << std::setfill('0');
        for (size_t i = 0; i < len; ++i) {
            ss << std::setw(2) << (int)data[i];
        }
        return ss.str();
    }

    std::vector<unsigned char> fromHex(const std::string& hex) {
        std::vector<unsigned char> bytes;
        bytes.reserve(hex.length() / 2);
        for (size_t i = 0; i < hex.length(); i += 2) {
            std::string byteString = hex.substr(i, 2);
            unsigned char byte = (unsigned char) strtol(byteString.c_str(), NULL, 16);
            bytes.push_back(byte);
        }
        return bytes;
    }
}

std::string CryptoHelper::deriveKey(const std::string& secret) {
    unsigned char hash[32];
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_MD_CTX");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, secret.data(), secret.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 digest failed in key derivation");
    }
    EVP_MD_CTX_free(ctx);
    return std::string((const char*)hash, 32);
}

std::string CryptoHelper::encryptAES256GCM(const std::string& plaintext, const std::string& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("AES-256-GCM key size must be 32 bytes");
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

    unsigned char iv[12];
    if (RAND_bytes(iv, sizeof(iv)) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to generate random GCM IV");
    }

    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to init GCM encrypt context");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM IV length");
    }

    if (EVP_EncryptInit_ex(ctx, NULL, NULL, (const unsigned char*)key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM key and IV");
    }

    std::vector<unsigned char> ciphertext(plaintext.size() + 16);
    int len = 0;
    int ciphertextLen = 0;

    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, (const unsigned char*)plaintext.data(), plaintext.size()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to encrypt update in GCM");
    }
    ciphertextLen = len;

    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to encrypt final in GCM");
    }
    ciphertextLen += len;

    unsigned char tag[16];
    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, sizeof(tag), tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to get GCM auth tag");
    }

    EVP_CIPHER_CTX_free(ctx);

    // Format: IV (12B hex = 24 chars) + Tag (16B hex = 32 chars) + Ciphertext (hex)
    std::string result = toHex(iv, sizeof(iv)) + toHex(tag, sizeof(tag)) + toHex(ciphertext.data(), ciphertextLen);
    return result;
}

std::string CryptoHelper::decryptAES256GCM(const std::string& hexCiphertext, const std::string& key) {
    if (key.size() != 32) {
        throw std::invalid_argument("AES-256-GCM key size must be 32 bytes");
    }
    if (hexCiphertext.size() < (12 + 16) * 2) {
        throw std::runtime_error("Ciphertext hex is too short for GCM format");
    }

    std::vector<unsigned char> data = fromHex(hexCiphertext);
    if (data.size() < 12 + 16) {
        throw std::runtime_error("Decoded GCM buffer is too short");
    }

    unsigned char iv[12];
    unsigned char tag[16];
    std::memcpy(iv, data.data(), 12);
    std::memcpy(tag, data.data() + 12, 16);

    size_t ciphertextLen = data.size() - 28;
    const unsigned char* ciphertext = data.data() + 28;

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_CIPHER_CTX");

    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to init GCM decrypt context");
    }

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, sizeof(iv), NULL) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM IV length");
    }

    if (EVP_DecryptInit_ex(ctx, NULL, NULL, (const unsigned char*)key.data(), iv) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM key and IV");
    }

    std::vector<unsigned char> decrypted(ciphertextLen + 16);
    int len = 0;
    int decryptedLen = 0;

    if (EVP_DecryptUpdate(ctx, decrypted.data(), &len, ciphertext, ciphertextLen) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to decrypt update in GCM");
    }
    decryptedLen = len;

    if (EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, sizeof(tag), tag) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        throw std::runtime_error("Failed to set GCM tag");
    }

    int ret = EVP_DecryptFinal_ex(ctx, decrypted.data() + len, &len);
    EVP_CIPHER_CTX_free(ctx);

    if (ret > 0) {
        decryptedLen += len;
        return std::string((const char*)decrypted.data(), decryptedLen);
    } else {
        throw std::runtime_error("AES-GCM decryption tag verification failed (tampered data)");
    }
}

std::string CryptoHelper::sha256(const std::string& plaintext) {
    unsigned char hash[32];
    unsigned int len = 0;
    EVP_MD_CTX* ctx = EVP_MD_CTX_new();
    if (!ctx) throw std::runtime_error("Failed to create EVP_MD_CTX");

    if (EVP_DigestInit_ex(ctx, EVP_sha256(), NULL) != 1 ||
        EVP_DigestUpdate(ctx, plaintext.data(), plaintext.size()) != 1 ||
        EVP_DigestFinal_ex(ctx, hash, &len) != 1) {
        EVP_MD_CTX_free(ctx);
        throw std::runtime_error("SHA256 digest failed");
    }
    EVP_MD_CTX_free(ctx);
    return toHex(hash, 32);
}

std::string CryptoHelper::hashPasswordPBKDF2(const std::string& password, const std::string& salt) {
    unsigned char hash[32];
    if (PKCS5_PBKDF2_HMAC(password.c_str(), password.size(),
                          (const unsigned char*)salt.c_str(), salt.size(),
                          10000, EVP_sha256(), 32, hash) != 1) {
        throw std::runtime_error("PBKDF2 password hashing failed");
    }
    return toHex(hash, 32);
}
