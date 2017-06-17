#pragma once

#include <memory>
#include <string>

#include "test/integration/integration.h"
#include "test/integration/server.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
using testing::NiceMock;

namespace Xfcc {

class XfccIntegrationTest : public BaseIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  const std::string previous_xfcc_ =
      "BY=spiffe://lyft.com/frontend;Hash=123456;SAN=spiffe://lyft.com/testclient";
  const std::string current_xfcc_by_hash_ =
      "BY=spiffe://lyft.com/"
      "backend-team;Hash=468ed33be74eee6556d90c0149c1309e9ba61d6425303443c0748a02dd8de688";
  const std::string client_san_ = "SAN=spiffe://lyft.com/frontend-team";

  XfccIntegrationTest() : BaseIntegrationTest(GetParam()) {}
  /**
   * Initializer for an individual test.
   */
  void SetUp() override;
  /**
   * Destructor for an individual test.
   */
  void TearDown() override;

  Ssl::ServerContextPtr createUpstreamSslContext();
  Ssl::ClientContextPtr createClientSslContext(bool mtls);
  Network::ClientConnectionPtr makeClientConnection();
  Network::ClientConnectionPtr makeTlsClientConnection();
  Network::ClientConnectionPtr makeMtlsClientConnection();
  void testRequestAndResponseWithXfccHeader(Network::ClientConnectionPtr&& conn,
                                            std::string privous_xfcc, std::string expected_xfcc);
  void startTestServerWithXfccConfig(std::string config, std::string content);

private:
  std::unique_ptr<Runtime::Loader> runtime_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Ssl::ClientContextPtr client_tls_ssl_ctx_;
  Ssl::ClientContextPtr client_mtls_ssl_ctx_;
  Ssl::ServerContextPtr upstream_ssl_ctx_;
};
} // Xfcc
} // Envoy
