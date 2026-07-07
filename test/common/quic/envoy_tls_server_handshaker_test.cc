#include <openssl/ssl.h>

#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {
namespace {

TEST(EnvoyTlsServerHandshakerTest, TicketKeyCallbackNullHandshaker) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(ssl_ctx, nullptr);
  bssl::UniquePtr<SSL> ssl(SSL_new(ssl_ctx.get()));
  ASSERT_NE(ssl, nullptr);
  // No ex_data set → returns 0 gracefully (no crash, no ENVOY_BUG).
  EXPECT_EQ(0, EnvoyTlsServerHandshaker::ticketKeyCallback(ssl.get(), nullptr, nullptr, nullptr,
                                                           nullptr, 0));
}

TEST(EnvoyTlsServerHandshakerTest, KeylogCallbackNullHandshaker) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(ssl_ctx, nullptr);
  bssl::UniquePtr<SSL> ssl(SSL_new(ssl_ctx.get()));
  ASSERT_NE(ssl, nullptr);
  // No ex_data set → silently no-ops (no crash, no ENVOY_BUG, no file write).
  EnvoyTlsServerHandshaker::keylogCallback(ssl.get(), "CLIENT_RANDOM 00 11");
}

} // namespace
} // namespace Quic
} // namespace Envoy
