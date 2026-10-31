#include "SignatureVerifier.h"

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <Windows.h>
#include <bcrypt.h>
#include <wincrypt.h>

#include <vector>

#pragma comment(lib, "bcrypt.lib")
#pragma comment(lib, "crypt32.lib")

namespace awow::updater {

thread_local std::string SignatureVerifier::lastError_;

namespace {

constexpr size_t kCoordinateSize = 32;   // P-256 coordinate width
constexpr size_t kSignatureSize = 64;    // r || s
constexpr size_t kDigestSize = 32;       // SHA-256

bool sha256(const uint8_t* data, size_t size, uint8_t digest[kDigestSize], std::string& error) {
    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_SHA256_ALGORITHM, nullptr, 0);
    if (status != 0) {
        error = "Failed to open SHA256 provider";
        return false;
    }

    status = BCryptHash(alg, nullptr, 0,
                        const_cast<PUCHAR>(data), static_cast<ULONG>(size),
                        digest, kDigestSize);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status != 0) {
        error = "Failed to hash data";
        return false;
    }
    return true;
}

bool decodeBase64(const std::string& text, std::vector<uint8_t>& outBytes, std::string& error) {
    DWORD byteCount = 0;
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              CRYPT_STRING_BASE64, nullptr, &byteCount, nullptr, nullptr)) {
        error = "Signature is not valid base64";
        return false;
    }

    outBytes.resize(byteCount);
    if (!CryptStringToBinaryA(text.c_str(), static_cast<DWORD>(text.size()),
                              CRYPT_STRING_BASE64, outBytes.data(), &byteCount, nullptr, nullptr)) {
        error = "Signature is not valid base64";
        return false;
    }

    outBytes.resize(byteCount);
    return true;
}

} // namespace

bool SignatureVerifier::verify(const uint8_t* data, size_t size,
                               const std::string& signatureBase64,
                               const uint8_t publicKey[64]) {
    lastError_.clear();

    // The signature is base64 of the raw r || s pair.
    std::vector<uint8_t> signature;
    if (!decodeBase64(signatureBase64, signature, lastError_)) {
        return false;
    }
    if (signature.size() != kSignatureSize) {
        lastError_ = "Signature has wrong length (" + std::to_string(signature.size()) +
                     " bytes, expected " + std::to_string(kSignatureSize) + ")";
        return false;
    }

    uint8_t digest[kDigestSize];
    if (!sha256(data, size, digest, lastError_)) {
        return false;
    }

    // BCRYPT_ECCKEY_BLOB header followed by X and Y.
    std::vector<uint8_t> keyBlob(sizeof(BCRYPT_ECCKEY_BLOB) + 2 * kCoordinateSize);
    auto* header = reinterpret_cast<BCRYPT_ECCKEY_BLOB*>(keyBlob.data());
    header->dwMagic = BCRYPT_ECDSA_PUBLIC_P256_MAGIC;
    header->cbKey = kCoordinateSize;
    memcpy(keyBlob.data() + sizeof(BCRYPT_ECCKEY_BLOB), publicKey, 2 * kCoordinateSize);

    BCRYPT_ALG_HANDLE alg = nullptr;
    NTSTATUS status = BCryptOpenAlgorithmProvider(&alg, BCRYPT_ECDSA_P256_ALGORITHM, nullptr, 0);
    if (status != 0) {
        lastError_ = "Failed to open ECDSA provider";
        return false;
    }

    BCRYPT_KEY_HANDLE key = nullptr;
    status = BCryptImportKeyPair(alg, nullptr, BCRYPT_ECCPUBLIC_BLOB, &key,
                                 keyBlob.data(), static_cast<ULONG>(keyBlob.size()), 0);
    if (status != 0) {
        BCryptCloseAlgorithmProvider(alg, 0);
        lastError_ = "Failed to import public key";
        return false;
    }

    status = BCryptVerifySignature(key, nullptr,
                                   digest, kDigestSize,
                                   signature.data(), static_cast<ULONG>(signature.size()),
                                   0);

    BCryptDestroyKey(key);
    BCryptCloseAlgorithmProvider(alg, 0);

    if (status != 0) {
        lastError_ = "Signature does not match";
        return false;
    }
    return true;
}

const std::string& SignatureVerifier::getLastError() {
    return lastError_;
}

} // namespace awow::updater
