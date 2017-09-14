#pragma once

#include <memory>
#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/mocks/runtime/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Xfcc {

class XfccIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  const std::string previous_xfcc_ =
      "By=spiffe://lyft.com/frontend;Hash=123456;SAN=spiffe://lyft.com/testclient";
  const std::string current_xfcc_by_hash_ =
      "By=spiffe://lyft.com/"
      "backend-team;Hash=9ed6533649269c694f717e8729a1c8b55006fd51013c0e0d4294916e00d7ea04";
  const std::string client_subject_ =
      "Subject=\""
      "emailAddress=frontend-team@lyft.com,CN=Test Frontend Team,"
      "OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US\"";
  const std::string client_san_ = "SAN=spiffe://lyft.com/frontend-team";

  XfccIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override;

  void SetUp() override { initialize(); }
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
} // namespace Xfcc
} // namespace Envoy
