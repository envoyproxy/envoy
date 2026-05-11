#include <openssl/ssl.h>

#include "source/common/quic/envoy_tls_server_handshaker.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {
namespace {

TEST(EnvoyTlsServerHandshakerTest, HandshakerExDataIndex) {
  int index = EnvoyTlsServerHandshaker::handshakerExDataIndex();
  EXPECT_GE(index, 0);
  // Calling again returns the same index.
  EXPECT_EQ(index, EnvoyTlsServerHandshaker::handshakerExDataIndex());
}

TEST(EnvoyTlsServerHandshakerTest, TicketKeyCallbackNullHandshaker) {
  bssl::UniquePtr<SSL_CTX> ssl_ctx(SSL_CTX_new(TLS_method()));
  ASSERT_NE(ssl_ctx, nullptr);
  bssl::UniquePtr<SSL> ssl(SSL_new(ssl_ctx.get()));
  ASSERT_NE(ssl, nullptr);
  // No ex_data set → returns 0 gracefully (no crash, no ENVOY_BUG).
  EXPECT_EQ(0, EnvoyTlsServerHandshaker::ticketKeyCallback(ssl.get(), nullptr, nullptr, nullptr,
                                                           nullptr, 0));
}

} // namespace
} // namespace Quic
} // namespace Envoy
