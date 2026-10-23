#include "source/common/tls/session_ticket_codec.h"

#include <algorithm>
#include <array>

#include "source/common/tls/context_impl.h"
#include "source/common/tls/ssl_handshaker.h"

#include "openssl/bytestring.h"
#include "openssl/err.h"
#include "openssl/hmac.h"
#include "openssl/mem.h"
#include "openssl/rand.h"
#include "openssl/x509.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {
#ifndef OPENSSL_IS_AWSLC
namespace {

constexpr std::array<uint8_t, 8> Magic{{'E', 'N', 'V', 'O', 'Y', 'V', 'C', 1}};
constexpr size_t MaxChainBytes = 48 * 1024;
constexpr size_t MaxChainCertificates = 64;
constexpr size_t KeyNameBytes = 16;
constexpr size_t IvBytes = 16;
constexpr size_t MacBytes = SHA256_DIGEST_LENGTH;
constexpr size_t TicketHeaderBytes = KeyNameBytes + IvBytes;
constexpr size_t MaxTicketBytes = 65535;

SslExtendedSocketInfoImpl* socketInfo(SSL* ssl) {
  auto* info = static_cast<Ssl::SslExtendedSocketInfo*>(
      SSL_get_ex_data(ssl, ContextImpl::sslExtendedSocketInfoIndex()));
  return dynamic_cast<SslExtendedSocketInfoImpl*>(info);
}

} // namespace

absl::StatusOr<std::unique_ptr<SessionTicketCodec>>
SessionTicketCodec::create(std::vector<Ssl::ServerContextConfig::SessionTicketKey> keys) {
  if (keys.empty()) {
    Ssl::ServerContextConfig::SessionTicketKey key;
    if (!RAND_bytes(key.name_.data(), key.name_.size()) ||
        !RAND_bytes(key.hmac_key_.data(), key.hmac_key_.size()) ||
        !RAND_bytes(key.aes_key_.data(), key.aes_key_.size())) {
      return absl::InternalError("Failed to generate session ticket keys");
    }
    keys.push_back(key);
  }
  return std::unique_ptr<SessionTicketCodec>(new SessionTicketCodec(std::move(keys)));
}

size_t SessionTicketCodec::maxOverhead() const {
  // Magic, two u24 lengths, the bounded chain, CBC padding, key name, IV and MAC.
  return Magic.size() + 6 + MaxChainBytes + IvBytes + TicketHeaderBytes + MacBytes;
}

int SessionTicketCodec::seal(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out_len,
                             const uint8_t* in, size_t in_len) const {
  *out_len = 0;
  std::vector<uint8_t> chain;
  auto* info = socketInfo(ssl);
  if (info != nullptr) {
    const auto certificates = info->validatedCertChainForSessionTicket();
    if (certificates.size() > MaxChainCertificates) {
      return 1; // Do not issue a ticket that cannot preserve the complete chain.
    }
    for (const auto& certificate : certificates) {
      const int length = i2d_X509(certificate.get(), nullptr);
      if (length <= 0 || static_cast<size_t>(length) + 3 > MaxChainBytes - chain.size()) {
        return 1;
      }
      chain.push_back(static_cast<uint8_t>(length >> 16));
      chain.push_back(static_cast<uint8_t>(length >> 8));
      chain.push_back(static_cast<uint8_t>(length));
      const size_t offset = chain.size();
      chain.resize(offset + length);
      uint8_t* cursor = chain.data() + offset;
      if (i2d_X509(certificate.get(), &cursor) != length) {
        return 0;
      }
    }
  }

  if (in_len > MaxTicketBytes ||
      in_len + chain.size() + maxOverhead() - MaxChainBytes > MaxTicketBytes) {
    return 1;
  }
  bssl::ScopedCBB cbb;
  CBB session, certificates;
  if (!CBB_init(cbb.get(), in_len + chain.size() + 16) ||
      !CBB_add_bytes(cbb.get(), Magic.data(), Magic.size()) ||
      !CBB_add_u24_length_prefixed(cbb.get(), &session) || !CBB_add_bytes(&session, in, in_len) ||
      !CBB_add_u24_length_prefixed(cbb.get(), &certificates) ||
      !CBB_add_bytes(&certificates, chain.data(), chain.size()) || !CBB_flush(cbb.get())) {
    return 0;
  }
  const size_t plaintext_len = CBB_len(cbb.get());
  if (max_out_len < TicketHeaderBytes + plaintext_len + IvBytes + MacBytes) {
    return 0;
  }

  const auto& key = keys_.front();
  std::copy(key.name_.begin(), key.name_.end(), out);
  if (!RAND_bytes(out + KeyNameBytes, IvBytes)) {
    return 0;
  }
  bssl::ScopedEVP_CIPHER_CTX cipher;
  int ciphertext_len, final_len;
  if (!EVP_EncryptInit_ex(cipher.get(), EVP_aes_256_cbc(), nullptr, key.aes_key_.data(),
                          out + KeyNameBytes) ||
      !EVP_EncryptUpdate(cipher.get(), out + TicketHeaderBytes, &ciphertext_len,
                         CBB_data(cbb.get()), plaintext_len) ||
      !EVP_EncryptFinal_ex(cipher.get(), out + TicketHeaderBytes + ciphertext_len, &final_len)) {
    return 0;
  }
  const size_t authenticated_len = TicketHeaderBytes + ciphertext_len + final_len;
  unsigned int mac_len;
  if (HMAC(EVP_sha256(), key.hmac_key_.data(), key.hmac_key_.size(), out, authenticated_len,
           out + authenticated_len, &mac_len) == nullptr ||
      mac_len != MacBytes) {
    return 0;
  }
  *out_len = authenticated_len + mac_len;
  return 1;
}

enum ssl_ticket_aead_result_t SessionTicketCodec::open(SSL* ssl, uint8_t* out, size_t* out_len,
                                                       size_t max_out_len, const uint8_t* in,
                                                       size_t in_len) const {
  *out_len = 0;
  auto* info = socketInfo(ssl);
  if (info != nullptr) {
    info->setResumedValidatedCertChain({});
  }
  if (in_len < TicketHeaderBytes + IvBytes + MacBytes || in_len > MaxTicketBytes) {
    return ssl_ticket_aead_ignore_ticket;
  }
  const auto key = std::find_if(keys_.begin(), keys_.end(), [in](const auto& candidate) {
    return std::equal(candidate.name_.begin(), candidate.name_.end(), in);
  });
  if (key == keys_.end()) {
    return ssl_ticket_aead_ignore_ticket;
  }
  const size_t authenticated_len = in_len - MacBytes;
  std::array<uint8_t, MacBytes> mac;
  unsigned int mac_len;
  // Authenticate before decrypting; untrusted ciphertext must never reach certificate parsing.
  if (HMAC(EVP_sha256(), key->hmac_key_.data(), key->hmac_key_.size(), in, authenticated_len,
           mac.data(), &mac_len) == nullptr ||
      mac_len != MacBytes) {
    return ssl_ticket_aead_error;
  }
  if (CRYPTO_memcmp(mac.data(), in + authenticated_len, MacBytes) != 0) {
    return ssl_ticket_aead_ignore_ticket;
  }
  const size_t ciphertext_len = authenticated_len - TicketHeaderBytes;
  std::vector<uint8_t> plaintext(ciphertext_len);
  bssl::ScopedEVP_CIPHER_CTX cipher;
  int plaintext_len, final_len;
  if (!EVP_DecryptInit_ex(cipher.get(), EVP_aes_256_cbc(), nullptr, key->aes_key_.data(),
                          in + KeyNameBytes) ||
      !EVP_DecryptUpdate(cipher.get(), plaintext.data(), &plaintext_len, in + TicketHeaderBytes,
                         ciphertext_len) ||
      !EVP_DecryptFinal_ex(cipher.get(), plaintext.data() + plaintext_len, &final_len)) {
    ERR_clear_error();
    return ssl_ticket_aead_ignore_ticket;
  }
  CBS payload, magic, session, certificates;
  CBS_init(&payload, plaintext.data(), plaintext_len + final_len);
  if (!CBS_get_bytes(&payload, &magic, Magic.size()) ||
      !std::equal(Magic.begin(), Magic.end(), CBS_data(&magic)) ||
      !CBS_get_u24_length_prefixed(&payload, &session) ||
      !CBS_get_u24_length_prefixed(&payload, &certificates) || CBS_len(&payload) != 0 ||
      CBS_len(&session) > max_out_len || CBS_len(&certificates) > MaxChainBytes) {
    return ssl_ticket_aead_ignore_ticket;
  }
  std::vector<bssl::UniquePtr<X509>> chain;
  while (CBS_len(&certificates) != 0) {
    CBS der;
    if (chain.size() == MaxChainCertificates || !CBS_get_u24_length_prefixed(&certificates, &der) ||
        CBS_len(&der) == 0) {
      return ssl_ticket_aead_ignore_ticket;
    }
    const uint8_t* cursor = CBS_data(&der);
    bssl::UniquePtr<X509> certificate(d2i_X509(nullptr, &cursor, CBS_len(&der)));
    if (!certificate || cursor != CBS_data(&der) + CBS_len(&der)) {
      ERR_clear_error();
      return ssl_ticket_aead_ignore_ticket;
    }
    chain.push_back(std::move(certificate));
  }
  if (info != nullptr) {
    // Stage the chain only. The handshaker must confirm the session and leaf before exposing it.
    info->setResumedValidatedCertChain(std::move(chain));
  }
  std::copy_n(CBS_data(&session), CBS_len(&session), out);
  *out_len = CBS_len(&session);
  return ssl_ticket_aead_success;
}

#else
absl::StatusOr<std::unique_ptr<SessionTicketCodec>>
SessionTicketCodec::create(std::vector<Ssl::ServerContextConfig::SessionTicketKey>) {
  return absl::UnimplementedError("Verified-chain session tickets require BoringSSL");
}
#endif

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
