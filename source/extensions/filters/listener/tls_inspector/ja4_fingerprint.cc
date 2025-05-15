#include "source/extensions/filters/listener/tls_inspector/ja4_fingerprint.h"

#include "source/common/common/hex.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_format.h"
#include "absl/strings/str_join.h"
#include "openssl/evp.h"
#include "openssl/sha.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace TlsInspector {

namespace {
// Google GREASE values as defined in RFC 8701
// These values are used to prevent ossification in TLS extensions and should be
// ignored when fingerprinting clients to prevent artificial differentiation
// See: https://datatracker.ietf.org/doc/html/rfc8701
const std::array<uint16_t, 16> GREASE_VALUES = {
    0x0a0a, 0x1a1a, 0x2a2a, 0x3a3a, 0x4a4a, 0x5a5a, 0x6a6a, 0x7a7a,
    0x8a8a, 0x9a9a, 0xaaaa, 0xbaba, 0xcaca, 0xdada, 0xeaea, 0xfafa,
};
} // namespace

bool JA4Fingerprinter::isNotGrease(uint16_t id) {
  return std::find(GREASE_VALUES.begin(), GREASE_VALUES.end(), id) == GREASE_VALUES.end();
}

absl::string_view JA4Fingerprinter::mapVersionToString(uint16_t version) {
  switch (version) {
  case TLS1_3_VERSION:
    return "13";
  case TLS1_2_VERSION:
    return "12";
  case TLS1_1_VERSION:
    return "11";
  case TLS1_VERSION:
    return "10";
  default:
    return "00";
  }
}

absl::string_view JA4Fingerprinter::getJA4TlsVersion(const SSL_CLIENT_HELLO* ssl_client_hello) {
  const uint8_t* data;
  size_t size;
  if (SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_supported_versions, &data,
                                           &size)) {
    CBS cbs;
    CBS_init(&cbs, data, size);
    CBS versions;

    if (!CBS_get_u8_length_prefixed(&cbs, &versions)) {
      return "00";
    }

    uint16_t highest_version = 0;
    uint16_t version;
    while (CBS_get_u16(&versions, &version)) {
      if (isNotGrease(version) && version > highest_version) {
        highest_version = version;
      }
    }

    if (highest_version != 0) {
      return mapVersionToString(highest_version);
    }
  }

  // Fallback to the protocol version if the supported_versions extension is not present
  return mapVersionToString(ssl_client_hello->version);
}

bool JA4Fingerprinter::hasSNI(const SSL_CLIENT_HELLO* ssl_client_hello) {
  const uint8_t* data;
  size_t size;
  return SSL_early_callback_ctx_extension_get(ssl_client_hello, TLSEXT_TYPE_server_name, &data,
                                              &size);
}

uint32_t JA4Fingerprinter::countCiphers(const SSL_CLIENT_HELLO* ssl_client_hello) {
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  uint32_t count = 0;
  while (CBS_len(&cipher_suites) > 0) {
    uint16_t cipher;
    if (!CBS_get_u16(&cipher_suites, &cipher)) {
      break;
    }
    if (isNotGrease(cipher)) {
      count++;
    }
  }

  return count;
}

uint32_t JA4Fingerprinter::countExtensions(const SSL_CLIENT_HELLO* ssl_client_hello) {
  CBS extensions;
  CBS_init(&extensions, ssl_client_hello->extensions, ssl_client_hello->extensions_len);

  uint32_t count = 0;
  while (CBS_len(&extensions) > 0) {
    uint16_t type;
    CBS extension;
    if (!CBS_get_u16(&extensions, &type) || !CBS_get_u16_length_prefixed(&extensions, &extension)) {
      break;
    }
    if (isNotGrease(type)) {
      count++;
    }
  }

  return count;
}

std::string JA4Fingerprinter::formatTwoDigits(uint32_t value) {
  return absl::StrFormat("%02d", std::min(value, 99u));
}

std::string JA4Fingerprinter::getJA4AlpnChars(const SSL_CLIENT_HELLO* ssl_client_hello) {
  const uint8_t* data;
  size_t size;
  if (!SSL_early_callback_ctx_extension_get(
          ssl_client_hello, TLSEXT_TYPE_application_layer_protocol_negotiation, &data, &size)) {
    return "00";
  }

  CBS cbs;
  CBS_init(&cbs, data, size);
  uint16_t list_length;
  if (!CBS_get_u16(&cbs, &list_length) || CBS_len(&cbs) < 1) {
    return "00";
  }

  uint8_t proto_length;
  if (!CBS_get_u8(&cbs, &proto_length) || CBS_len(&cbs) < proto_length) {
    return "00";
  }

  const uint8_t* proto_data = CBS_data(&cbs);
  absl::string_view proto(reinterpret_cast<const char*>(proto_data), proto_length);

  if (proto.empty()) {
    return "00";
  }

  char first = proto[0];
  char last = proto[proto.length() - 1];
  if (!absl::ascii_isalnum(first) || !absl::ascii_isalnum(last)) {
    // Convert to hex if non-alphanumeric
    return absl::StrFormat("%02x%02x", static_cast<uint8_t>(first), static_cast<uint8_t>(last));
  }
  return absl::StrFormat("%c%c", first, last);
}

std::string JA4Fingerprinter::getJA4CipherHash(const SSL_CLIENT_HELLO* ssl_client_hello) {
  CBS cipher_suites;
  CBS_init(&cipher_suites, ssl_client_hello->cipher_suites, ssl_client_hello->cipher_suites_len);

  std::vector<uint16_t> ciphers;
  // Each cipher suite is 2 bytes long, so we reserve half the length of the buffer
  ciphers.reserve(ssl_client_hello->cipher_suites_len / 2);
  while (CBS_len(&cipher_suites) > 0) {
    uint16_t cipher;
    if (!CBS_get_u16(&cipher_suites, &cipher)) {
      break;
    }
    if (isNotGrease(cipher)) {
      ciphers.push_back(cipher);
    }
  }

  if (ciphers.empty()) {
    return std::string(JA4_HASH_LENGTH, '0');
  }

  std::sort(ciphers.begin(), ciphers.end());

  // Pre-allocates a buffer for the cipher list with a reasonable size estimation
  // Format: "0000,0000,0000..." - 5 bytes per cipher (4 hex + delimiter)
  std::string cipher_list;
  cipher_list.reserve(ciphers.size() * 5);

  for (size_t i = 0; i < ciphers.size(); ++i) {
    if (i > 0) {
      cipher_list.push_back(',');
    }

    // Format the 16-bit value as a 4-character hex string
    char hex_buffer[5]; // 4 chars + null terminator
    snprintf(hex_buffer, sizeof(hex_buffer), "%04x", ciphers[i]);
    cipher_list.append(hex_buffer);
  }

  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash;
  EVP_Digest(cipher_list.data(), cipher_list.length(), hash.data(), nullptr, EVP_sha256(), nullptr);

  return Envoy::Hex::encode(hash.data(), JA4_HASH_LENGTH / 2);
}

std::string JA4Fingerprinter::getJA4ExtensionHash(const SSL_CLIENT_HELLO* ssl_client_hello) {
  std::vector<uint16_t> extensions;
  std::vector<uint16_t> sig_algs;
  CBS exts;
  CBS_init(&exts, ssl_client_hello->extensions, ssl_client_hello->extensions_len);

  while (CBS_len(&exts) > 0) {
    uint16_t type;
    CBS extension;
    if (!CBS_get_u16(&exts, &type) || !CBS_get_u16_length_prefixed(&exts, &extension)) {
      break;
    }

    if (isNotGrease(type) && type != TLSEXT_TYPE_server_name &&
        type != TLSEXT_TYPE_application_layer_protocol_negotiation) {
      extensions.push_back(type);

      // Collect signature algorithms
      if (type == TLSEXT_TYPE_signature_algorithms) {
        CBS sig_alg_data;
        CBS_init(&sig_alg_data, CBS_data(&extension), CBS_len(&extension));
        uint16_t sig_alg_len;
        if (CBS_get_u16(&sig_alg_data, &sig_alg_len)) {
          while (CBS_len(&sig_alg_data) >= 2) {
            uint16_t sig_alg;
            if (!CBS_get_u16(&sig_alg_data, &sig_alg)) {
              break;
            }
            sig_algs.push_back(sig_alg);
          }
        }
      }
    }
  }

  if (extensions.empty()) {
    return std::string(JA4_HASH_LENGTH, '0');
  }

  std::sort(extensions.begin(), extensions.end());

  // Pre-allocate a buffer for the extension list with a reasonable size estimation
  // Format: "0000,0000,0000..." - 5 bytes per extension (4 hex + delimiter)
  std::string extension_list;
  extension_list.reserve(extensions.size() * 5 + (sig_algs.empty() ? 0 : sig_algs.size() * 5 + 1));

  for (size_t i = 0; i < extensions.size(); ++i) {
    if (i > 0) {
      extension_list.push_back(',');
    }

    // Format the 16-bit value as a 4-character hex string
    char hex_buffer[5]; // 4 chars + null terminator
    snprintf(hex_buffer, sizeof(hex_buffer), "%04x", extensions[i]);
    extension_list.append(hex_buffer);
  }

  if (!sig_algs.empty()) {
    extension_list.push_back('_');
    for (size_t i = 0; i < sig_algs.size(); ++i) {
      if (i > 0) {
        extension_list.push_back(',');
      }

      // Format the 16-bit value as a 4-character hex string
      char hex_buffer[5]; // 4 chars + null terminator
      snprintf(hex_buffer, sizeof(hex_buffer), "%04x", sig_algs[i]);
      extension_list.append(hex_buffer);
    }
  }

  std::array<uint8_t, SHA256_DIGEST_LENGTH> hash;
  EVP_Digest(extension_list.data(), extension_list.length(), hash.data(), nullptr, EVP_sha256(),
             nullptr);

  return Envoy::Hex::encode(hash.data(), JA4_HASH_LENGTH / 2);
}

std::string JA4Fingerprinter::create(const SSL_CLIENT_HELLO* ssl_client_hello) {
  return absl::StrCat(
      // Protocol type (t for TLS, q for QUIC, d for `DTLS`)
      // In this implementation, we only handle TLS
      "t",

      // TLS Version
      getJA4TlsVersion(ssl_client_hello),

      // SNI presence
      hasSNI(ssl_client_hello) ? "d" : "i",

      // Cipher count
      formatTwoDigits(countCiphers(ssl_client_hello)),

      // Extension count
      formatTwoDigits(countExtensions(ssl_client_hello)),

      // ALPN first/last chars
      getJA4AlpnChars(ssl_client_hello),

      // Separator
      "_",

      // Cipher hash
      getJA4CipherHash(ssl_client_hello),

      // Separator
      "_",

      // Extension and signature algorithm hash
      getJA4ExtensionHash(ssl_client_hello));
}

} // namespace TlsInspector
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
