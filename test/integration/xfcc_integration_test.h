#pragma once

#include <memory>
#include <string>

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/server/mocks.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Xfcc {

class XfccIntegrationTest : public HttpIntegrationTest,
                            public testing::TestWithParam<Network::Address::IpVersion> {
public:
  const std::string previous_xfcc_ =
      "By=spiffe://lyft.com/frontend;Hash=123456;URI=spiffe://lyft.com/testclient";
  const std::string current_xfcc_by_hash_ =
      "By=spiffe://lyft.com/"
      "backend-team;Hash=e0f3c8ce5e2ea305f0701ff512e36e2e97928284a228bcf77332d33930a1b6fd";
  const std::string client_subject_ =
      "Subject=\""
      "emailAddress=frontend-team@lyft.com,CN=Test Frontend Team,"
      "OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US\"";
  const std::string client_uri_san_ = "URI=spiffe://lyft.com/frontend-team";
  const std::string client_dns_san_ = "DNS=lyft.com;DNS=www.lyft.com";

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
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      ForwardClientCertDetails fcc_;
  envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager::
      SetCurrentClientCertDetails sccd_;
  bool tls_ = true;

private:
  std::unique_ptr<Runtime::Loader> runtime_;
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr client_tls_ssl_ctx_;
  Network::TransportSocketFactoryPtr client_mtls_ssl_ctx_;
  Network::TransportSocketFactoryPtr upstream_ssl_ctx_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};
} // namespace Xfcc
} // namespace Envoy
