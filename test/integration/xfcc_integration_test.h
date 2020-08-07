#pragma once

#include <memory>
#include <string>

#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/mocks/server/transport_socket_factory_context.h"

#include "absl/strings/ascii.h"
#include "absl/strings/str_replace.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Xfcc {

class XfccIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                            public HttpIntegrationTest {
public:
  const std::string previous_xfcc_ =
      "By=spiffe://lyft.com/frontend;Hash=123456;URI=spiffe://lyft.com/testclient";
  const std::string current_xfcc_by_hash_ =
      "By=spiffe://lyft.com/"
      "backend-team;Hash=" +
      absl::AsciiStrToLower(absl::StrReplaceAll(TEST_CLIENT_CERT_HASH, {{":", ""}}));
  const std::string client_subject_ =
      "Subject=\""
      "emailAddress=frontend-team@lyft.com,CN=Test Frontend Team,"
      "OU=Lyft Engineering,O=Lyft,L=San Francisco,ST=California,C=US\"";
  const std::string client_uri_san_ = "URI=spiffe://lyft.com/frontend-team";
  const std::string client_dns_san_ = "DNS=lyft.com;DNS=www.lyft.com";

  XfccIntegrationTest() : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {
    ON_CALL(factory_context_, api()).WillByDefault(ReturnRef(*api_));
  }

  void initialize() override;
  void createUpstreams() override;

  void TearDown() override;

  Network::TransportSocketFactoryPtr createUpstreamSslContext();
  Network::TransportSocketFactoryPtr createClientSslContext(bool mtls);
  Network::ClientConnectionPtr makeTcpClientConnection();
  Network::ClientConnectionPtr makeTlsClientConnection();
  Network::ClientConnectionPtr makeMtlsClientConnection();
  void testRequestAndResponseWithXfccHeader(std::string privous_xfcc, std::string expected_xfcc);
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      ForwardClientCertDetails fcc_;
  envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager::
      SetCurrentClientCertDetails sccd_;
  bool tls_ = true;

private:
  std::unique_ptr<Ssl::ContextManager> context_manager_;
  Network::TransportSocketFactoryPtr client_tls_ssl_ctx_;
  Network::TransportSocketFactoryPtr client_mtls_ssl_ctx_;
  Network::TransportSocketFactoryPtr upstream_ssl_ctx_;
  testing::NiceMock<Server::Configuration::MockTransportSocketFactoryContext> factory_context_;
};
} // namespace Xfcc
} // namespace Envoy
