#pragma once
// ═══════════════════════════════════════════════════════════════════════════════
//  passkey_manager.h — pure P-256 ECDSA crypto for WebAuthn passkeys
// ═══════════════════════════════════════════════════════════════════════════════
// Stateless crypto only -- no vault storage, no BLE, no locking. VaultManager
// owns storage (generateAndAddPasskey/signWithPasskey); this module owns the
// math. Mirrors the exact mbedtls P-256 pattern already proven in
// secure_session.cpp's ECDH keypair generation (same curve, same
// heap-allocated-context style to keep stack usage low on small-stack tasks).
#include <Arduino.h>
#include <stdint.h>
#include <stddef.h>

namespace PasskeyManager {

// Raw sizes (NOT base64 -- see vault_types.h's PASSKEY_* lengths for the
// base64url string buffer sizes used in storage).
static const size_t P256_PRIVATE_KEY_LEN = 32;             // scalar d
static const size_t P256_PUBLIC_KEY_LEN  = 65;              // 0x04 || X || Y
static const size_t P256_SIGNATURE_LEN   = 64;              // r || s (WebAuthn raw format)
static const size_t SHA256_HASH_LEN      = 32;

// Generates a fresh P-256 keypair using the ESP32's hardware RNG (via
// mbedtls_ctr_drbg seeded from mbedtls_entropy, identical seeding pattern
// to secure_session.cpp). privOut/pubOut must be the sizes above.
// Returns false on any mbedtls failure (never partially fills the buffers).
bool generateKeypair(uint8_t privOut32[32], uint8_t pubOut65[65]);

// ECDSA-sign a 32-byte SHA-256 digest with the given P-256 private scalar.
// Writes the signature as raw r||s (32 + 32 bytes, WebAuthn's expected
// format -- NOT the ASN.1 DER mbedtls_ecdsa_write_signature() produces).
bool signHash(const uint8_t priv32[32], const uint8_t hash32[32],
              uint8_t sigOut64[64]);

// SHA-256 of an arbitrary buffer -- convenience wrapper (mbedtls_sha256 is
// already used elsewhere in this codebase, e.g. crypto_utils.cpp).
void sha256(const uint8_t* data, size_t len, uint8_t out32[32]);

// Base64url (RFC 4648 §5 -- '-'/'_' alphabet, NO padding), the encoding
// WebAuthn uses for credential IDs, public keys, and challenges.
// Returns the encoded length (0 on failure, e.g. outCap too small).
size_t base64urlEncode(const uint8_t* in, size_t inLen, char* out, size_t outCap);

// Decodes a base64url string into raw bytes. Returns the decoded length,
// or 0 on failure (invalid input or outCap too small).
size_t base64urlDecode(const char* in, size_t inLen, uint8_t* out, size_t outCap);

}  // namespace PasskeyManager
