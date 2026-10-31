#include "../UnifiedSettings.h"
#include "AwowLabBuildOptions.h"

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

std::vector<uint8_t> UnifiedSettings::deriveKey(std::string_view material) {
    std::vector<uint8_t> key(KEY_SIZE);
    SHA256(reinterpret_cast<const unsigned char*>(material.data()),
           material.size(),
           key.data());

    return key;
}

std::vector<uint8_t> UnifiedSettings::deriveKey() {
    return deriveKey(AWOWLAB_SETTINGS_KEY);
}

std::vector<uint8_t> UnifiedSettings::encrypt(const std::vector<uint8_t>& plaintext, std::vector<uint8_t>& iv) {
    iv.resize(IV_SIZE);
    if (RAND_bytes(iv.data(), IV_SIZE) != 1) {
        return {};  // The caller logs the failed save
    }

    auto key = deriveKey();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }

    std::vector<uint8_t> ciphertext;
    ciphertext.resize(plaintext.size() + EVP_MAX_BLOCK_LENGTH);

    int len = 0;
    int ciphertextLen = 0;

    // Initialize encryption
    if (EVP_EncryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Encrypt
    if (EVP_EncryptUpdate(ctx, ciphertext.data(), &len, plaintext.data(), static_cast<int>(plaintext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertextLen = len;

    // Finalize (adds PKCS7 padding)
    if (EVP_EncryptFinal_ex(ctx, ciphertext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    ciphertextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    ciphertext.resize(ciphertextLen);
    return ciphertext;
}

std::vector<uint8_t> UnifiedSettings::decrypt(const std::vector<uint8_t>& ciphertext, const std::vector<uint8_t>& iv) {
    if (iv.size() != IV_SIZE || ciphertext.empty()) {
        return {};
    }

    auto key = deriveKey();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        return {};
    }

    std::vector<uint8_t> plaintext;
    plaintext.resize(ciphertext.size() + EVP_MAX_BLOCK_LENGTH);

    int len = 0;
    int plaintextLen = 0;

    // Initialize decryption
    if (EVP_DecryptInit_ex(ctx, EVP_aes_256_cbc(), nullptr, key.data(), iv.data()) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }

    // Decrypt
    if (EVP_DecryptUpdate(ctx, plaintext.data(), &len, ciphertext.data(), static_cast<int>(ciphertext.size())) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};
    }
    plaintextLen = len;

    // Finalize (removes PKCS7 padding)
    if (EVP_DecryptFinal_ex(ctx, plaintext.data() + len, &len) != 1) {
        EVP_CIPHER_CTX_free(ctx);
        return {};  // Decryption failed (wrong key or corrupted data)
    }
    plaintextLen += len;

    EVP_CIPHER_CTX_free(ctx);

    plaintext.resize(plaintextLen);
    return plaintext;
}
