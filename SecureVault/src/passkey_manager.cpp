#include "passkey_manager.h"
#include "crypto_utils.h"  // secureZero
#include <mbedtls/ecp.h>
#include <mbedtls/ecdsa.h>
#include <mbedtls/bignum.h>
#include <mbedtls/entropy.h>
#include <mbedtls/ctr_drbg.h>
#include <mbedtls/sha256.h>
#include <string.h>

namespace PasskeyManager {

bool generateKeypair(uint8_t privOut32[32], uint8_t pubOut65[65]) {
  // Heap-allocated contexts -- same reasoning as secure_session.cpp's
  // _generateEcdhKeypair(): keeps this call's stack footprint small
  // enough to be safe from any task (BLE callback tasks included, which
  // can have even smaller stacks than AsyncWebServer's).
  mbedtls_ecp_group* grp = new mbedtls_ecp_group;
  mbedtls_mpi* d = new mbedtls_mpi;
  mbedtls_ecp_point* Q = new mbedtls_ecp_point;
  mbedtls_entropy_context* entropy = new mbedtls_entropy_context;
  mbedtls_ctr_drbg_context* ctr_drbg = new mbedtls_ctr_drbg_context;
  bool ok = false;

  mbedtls_ecp_group_init(grp);
  mbedtls_mpi_init(d);
  mbedtls_ecp_point_init(Q);
  mbedtls_entropy_init(entropy);
  mbedtls_ctr_drbg_init(ctr_drbg);

  const char* pers = "SecureVault-passkey-keygen";
  if (mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy,
                             (const unsigned char*)pers, strlen(pers)) != 0) {
    goto cleanup;
  }

  if (mbedtls_ecp_group_load(grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    goto cleanup;
  }

  yield();
  if (mbedtls_ecp_gen_keypair(grp, d, Q, mbedtls_ctr_drbg_random, ctr_drbg) != 0) {
    goto cleanup;
  }
  yield();

  if (mbedtls_mpi_write_binary(d, privOut32, P256_PRIVATE_KEY_LEN) != 0) {
    goto cleanup;
  }

  {
    size_t olen = 0;
    if (mbedtls_ecp_point_write_binary(grp, Q, MBEDTLS_ECP_PF_UNCOMPRESSED,
                                        &olen, pubOut65, P256_PUBLIC_KEY_LEN) != 0 ||
        olen != P256_PUBLIC_KEY_LEN) {
      goto cleanup;
    }
  }

  ok = true;

cleanup:
  mbedtls_ecp_group_free(grp);
  mbedtls_mpi_free(d);
  mbedtls_ecp_point_free(Q);
  mbedtls_entropy_free(entropy);
  mbedtls_ctr_drbg_free(ctr_drbg);
  delete grp;
  delete d;
  delete Q;
  delete entropy;
  delete ctr_drbg;

  if (!ok) {
    // Never leave a partial/garbage private key sitting in the caller's
    // buffer on failure.
    secureZero(privOut32, P256_PRIVATE_KEY_LEN);
  }
  return ok;
}

bool signHash(const uint8_t priv32[32], const uint8_t hash32[32], uint8_t sigOut64[64]) {
  mbedtls_ecp_group* grp = new mbedtls_ecp_group;
  mbedtls_mpi* d = new mbedtls_mpi;
  mbedtls_mpi* r = new mbedtls_mpi;
  mbedtls_mpi* s = new mbedtls_mpi;
  mbedtls_entropy_context* entropy = new mbedtls_entropy_context;
  mbedtls_ctr_drbg_context* ctr_drbg = new mbedtls_ctr_drbg_context;
  bool ok = false;

  mbedtls_ecp_group_init(grp);
  mbedtls_mpi_init(d);
  mbedtls_mpi_init(r);
  mbedtls_mpi_init(s);
  mbedtls_entropy_init(entropy);
  mbedtls_ctr_drbg_init(ctr_drbg);

  const char* pers = "SecureVault-passkey-sign";
  if (mbedtls_ctr_drbg_seed(ctr_drbg, mbedtls_entropy_func, entropy,
                             (const unsigned char*)pers, strlen(pers)) != 0) {
    goto cleanup;
  }

  if (mbedtls_ecp_group_load(grp, MBEDTLS_ECP_DP_SECP256R1) != 0) {
    goto cleanup;
  }

  if (mbedtls_mpi_read_binary(d, priv32, P256_PRIVATE_KEY_LEN) != 0) {
    goto cleanup;
  }

  yield();
  // Deterministic-RNG-seeded ECDSA (RFC 6979 not used here -- plain
  // randomized ECDSA via mbedtls_ecdsa_sign, same as any standard P-256
  // signer; a fresh random k is drawn from ctr_drbg for every signature,
  // which is the security-critical property -- k must never repeat).
  if (mbedtls_ecdsa_sign(grp, r, s, d, hash32, SHA256_HASH_LEN,
                         mbedtls_ctr_drbg_random, ctr_drbg) != 0) {
    goto cleanup;
  }
  yield();

  // WebAuthn/CBOR assertions expect raw r||s (32 + 32 bytes, zero-padded),
  // NOT the ASN.1 DER sequence mbedtls_ecdsa_write_signature() would give.
  if (mbedtls_mpi_write_binary(r, sigOut64, 32) != 0) goto cleanup;
  if (mbedtls_mpi_write_binary(s, sigOut64 + 32, 32) != 0) goto cleanup;

  ok = true;

cleanup:
  mbedtls_ecp_group_free(grp);
  mbedtls_mpi_free(d);
  mbedtls_mpi_free(r);
  mbedtls_mpi_free(s);
  mbedtls_entropy_free(entropy);
  mbedtls_ctr_drbg_free(ctr_drbg);
  delete grp;
  delete d;
  delete r;
  delete s;
  delete entropy;
  delete ctr_drbg;
  return ok;
}

void sha256(const uint8_t* data, size_t len, uint8_t out32[32]) {
  mbedtls_sha256_context ctx;
  mbedtls_sha256_init(&ctx);
  mbedtls_sha256_starts(&ctx, 0);  // 0 = SHA-256 (not SHA-224)
  mbedtls_sha256_update(&ctx, data, len);
  mbedtls_sha256_finish(&ctx, out32);
  mbedtls_sha256_free(&ctx);
}

static const char* B64URL_ALPHABET =
  "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";

size_t base64urlEncode(const uint8_t* in, size_t inLen, char* out, size_t outCap) {
  size_t outIdx = 0, bits = 0;
  uint32_t buf = 0;
  for (size_t i = 0; i < inLen; i++) {
    buf = (buf << 8) | in[i];
    bits += 8;
    while (bits >= 6) {
      bits -= 6;
      if (outIdx + 1 >= outCap) return 0;  // +1 reserves room for the NUL
      out[outIdx++] = B64URL_ALPHABET[(buf >> bits) & 0x3F];
    }
  }
  if (bits > 0) {
    if (outIdx + 1 >= outCap) return 0;
    out[outIdx++] = B64URL_ALPHABET[(buf << (6 - bits)) & 0x3F];
  }
  if (outIdx >= outCap) return 0;
  out[outIdx] = '\0';
  return outIdx;
}

static int8_t b64urlDecodeChar(char c) {
  if (c >= 'A' && c <= 'Z') return c - 'A';
  if (c >= 'a' && c <= 'z') return c - 'a' + 26;
  if (c >= '0' && c <= '9') return c - '0' + 52;
  if (c == '-') return 62;
  if (c == '_') return 63;
  return -1;
}

size_t base64urlDecode(const char* in, size_t inLen, uint8_t* out, size_t outCap) {
  size_t outIdx = 0, bits = 0;
  uint32_t buf = 0;
  for (size_t i = 0; i < inLen; i++) {
    if (in[i] == '=') break;  // tolerate stray padding some callers may send
    int8_t v = b64urlDecodeChar(in[i]);
    if (v < 0) return 0;  // invalid character
    buf = (buf << 6) | (uint8_t)v;
    bits += 6;
    if (bits >= 8) {
      bits -= 8;
      if (outIdx >= outCap) return 0;
      out[outIdx++] = (uint8_t)((buf >> bits) & 0xFF);
    }
  }
  return outIdx;
}

}  // namespace PasskeyManager
