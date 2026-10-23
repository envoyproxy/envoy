#pragma once

#include <memory>
#include <vector>

#include "envoy/ssl/context_config.h"

#include "absl/status/statusor.h"
#include "openssl/ssl.h"

namespace Envoy {
namespace Extensions {
namespace TransportSockets {
namespace Tls {

// Experimental authenticated ticket envelope carrying the originally verified peer chain.
// The embedded BoringSSL session is still responsible for deciding whether resumption is allowed.
class SessionTicketCodec {
public:
  static absl::StatusOr<std::unique_ptr<SessionTicketCodec>>
  create(std::vector<Ssl::ServerContextConfig::SessionTicketKey> keys);

#ifndef OPENSSL_IS_AWSLC
  size_t maxOverhead() const;
  int seal(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out_len, const uint8_t* in,
           size_t in_len) const;
  enum ssl_ticket_aead_result_t open(SSL* ssl, uint8_t* out, size_t* out_len, size_t max_out_len,
                                     const uint8_t* in, size_t in_len) const;
#endif

private:
  explicit SessionTicketCodec(std::vector<Ssl::ServerContextConfig::SessionTicketKey> keys)
      : keys_(std::move(keys)) {}

  const std::vector<Ssl::ServerContextConfig::SessionTicketKey> keys_;
};

} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
