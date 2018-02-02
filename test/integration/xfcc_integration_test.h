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
  void createUpstreams() override;

  void TearDown() override;

  Network::TransportSocketFactoryPtr createUpstreamSslContext();
  Network::TransportSocketFactoryPtr createClientSslContext(bool mtls);
  Network::ClientConnectionPtr makeClientConnection();
  Network::ClientConnectionPtr makeTlsClientConnection();
  Network::ClientConnectionPtr makeMtlsClientConnection();
  void testRequestAndResponseWithXfccHeader(std::string privous_xfcc, std::string expected_xfcc);
  envoy::api::v2::filter::network::HttpConnectionManager::ForwardClientCertDetails fcc_;
  envoy::api::v2::filter::network::HttpConnectionManager::SetCurrentClientCertDetails sccd_;
  bool tls_ = true;

private:
  std::unique_ptr<Runtime::Loader> runtime_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr client_tls_ssl_ctx_;
  Network::TransportSocketFactoryPtr client_mtls_ssl_ctx_;
  Network::TransportSocketFactoryPtr upstream_ssl_ctx_;
};
} // namespace Xfcc
} // namespace Envoy
