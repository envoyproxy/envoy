#pragma once

#include <string>

#include "envoy/stats/stats_macros.h"

#include "openssl/ssl.h"

namespace Envoy {
namespace Ssl {
namespace Utility {

/**
 * All stats for the TLS inspector. @see stats_macros.h
 */
#define TLS_STATS(COUNTER)                                                                         \
  COUNTER(connection_closed)                                                                       \
  COUNTER(client_hello_too_large)                                                                  \
  COUNTER(read_error)                                                                              \
  COUNTER(read_timeout)                                                                            \
  COUNTER(tls_found)                                                                               \
  COUNTER(tls_not_found)                                                                           \
  COUNTER(alpn_found)                                                                              \
  COUNTER(alpn_not_found)                                                                          \
  COUNTER(sni_found)                                                                               \
  COUNTER(sni_not_found)

/**
 * Definition of stats for the TLS. @see stats_macros.h
 */
struct TlsStats {
  TLS_STATS(GENERATE_COUNTER_STRUCT)
};

/**
 * Retrieve the serial number of a certificate.
 * @param ssl the certificate
 * @return std::string the serial number field of the certificate. Returns "" if
 *         there is no serial number.
 */
std::string getSerialNumberFromCertificate(X509& cert);

void parseClientHello(const void* data, size_t len, bssl::UniquePtr<SSL>& ssl_, uint64_t read_,
                      uint32_t maxClientHelloSize, const Ssl::Utility::TlsStats& stats,
                      std::function<void(bool)>, bool& alpn_found_, bool& clienthello_success_,
                      std::function<void()> onSuccess);

} // namespace Utility
} // namespace Ssl
} // namespace Envoy
