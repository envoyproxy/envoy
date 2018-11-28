#pragma once

#include <memory>
#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
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

  Network::ClientConnectionPtr makeSslConn() { return makeSslClientConnection({}); }
  Network::ClientConnectionPtr makeSslClientConnection(const ClientSslTransportOptions& options);
  void checkStats();

protected:
  bool server_ecdsa_cert_{false};
  // Set this true to debug SSL handshake issues with openssl s_client. The
  // verbose trace will be in the logs, openssl must be installed separately.
  bool debug_with_s_client_{false};

private:
  std::unique_ptr<ContextManager> context_manager_;
};

} // namespace Ssl
} // namespace Envoy
