#pragma once

#include <memory>
#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/mocks/secret/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Ssl {

class SslIntegrationTest : public HttpIntegrationTest,
                           public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SslIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam(), realTime()) {}

  void initialize() override;

  void TearDown() override;

  Network::ClientConnectionPtr makeSslConn() { return makeSslClientConnection(false, false); }
  Network::ClientConnectionPtr makeSslClientConnection(bool alpn, bool san);
  void checkStats();

protected:
  // Set this true to debug SSL handshake issues with openssl s_client. The
  // verbose trace will be in the logs, openssl must be installed separately.
  bool debug_with_s_client_{false};

private:
  std::unique_ptr<ContextManager> context_manager_;

  Network::TransportSocketFactoryPtr client_ssl_ctx_plain_;
  Network::TransportSocketFactoryPtr client_ssl_ctx_alpn_;
  Network::TransportSocketFactoryPtr client_ssl_ctx_san_;
  Network::TransportSocketFactoryPtr client_ssl_ctx_alpn_san_;
};

} // namespace Ssl
} // namespace Envoy
