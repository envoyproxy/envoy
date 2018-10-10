#include <memory>
#include <string>

#include "envoy/stats/scope.h"

#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"
#include "common/ssl/context_config_impl.h"
#include "common/ssl/context_manager_impl.h"

#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/init/mocks.h"
#include "test/mocks/runtime/mocks.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Ssl {

class SdsStaticDownstreamIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SdsStaticDownstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* common_tls_context = bootstrap.mutable_static_resources()
                                     ->mutable_listeners(0)
                                     ->mutable_filter_chains(0)
                                     ->mutable_tls_context()
                                     ->mutable_common_tls_context();
      common_tls_context->add_alpn_protocols("http/1.1");

      auto* validation_context = common_tls_context->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      validation_context->add_verify_certificate_hash(
          "E0:F3:C8:CE:5E:2E:A3:05:F0:70:1F:F5:12:E3:6E:2E:"
          "97:92:82:84:A2:28:BC:F7:73:32:D3:39:30:A1:B6:FD");

      common_tls_context->add_tls_certificate_sds_secret_configs()->set_name("server_cert");

      auto* secret = bootstrap.mutable_static_resources()->add_secrets();
      secret->set_name("server_cert");
      auto* tls_certificate = secret->mutable_tls_certificate();
      tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("/test/config/integration/certs/servercert.pem"));
      tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("/test/config/integration/certs/serverkey.pem"));
    });

    HttpIntegrationTest::initialize();

    registerTestServerPorts({"http"});

    client_ssl_ctx_ = createClientSslTransportSocketFactory(false, false, context_manager_);
  }

  void TearDown() override {
    client_ssl_ctx_.reset();
    cleanupUpstreamAndDownstream();
    fake_upstream_connection_.reset();
    codec_client_.reset();
  }

  Network::ClientConnectionPtr makeSslClientConnection() {
    Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               client_ssl_ctx_->createTransportSocket(), nullptr);
  }

private:
  Runtime::MockLoader runtime_;
  Ssl::ContextManagerImpl context_manager_{runtime_};

  Network::TransportSocketFactoryPtr client_ssl_ctx_;
};

INSTANTIATE_TEST_CASE_P(IpVersions, SdsStaticDownstreamIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SdsStaticDownstreamIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, &creator);
}

class SdsStaticUpstreamIntegrationTest
    : public HttpIntegrationTest,
      public testing::TestWithParam<Network::Address::IpVersion> {
public:
  SdsStaticUpstreamIntegrationTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, GetParam()) {}

  void initialize() override {
    config_helper_.addConfigModifier([](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      bootstrap.mutable_static_resources()
          ->mutable_clusters(0)
          ->mutable_tls_context()
          ->mutable_common_tls_context()
          ->add_tls_certificate_sds_secret_configs()
          ->set_name("client_cert");

      auto* secret = bootstrap.mutable_static_resources()->add_secrets();
      secret->set_name("client_cert");
      auto* tls_certificate = secret->mutable_tls_certificate();
      tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("/test/config/integration/certs/clientcert.pem"));
      tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("/test/config/integration/certs/clientkey.pem"));
    });

    HttpIntegrationTest::initialize();

    registerTestServerPorts({"http"});
  }

  void TearDown() override {
    cleanupUpstreamAndDownstream();
    fake_upstream_connection_.reset();
    codec_client_.reset();

    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    fake_upstreams_.emplace_back(new FakeUpstream(createUpstreamSslContext(context_manager_), 0,
                                                  FakeHttpConnection::Type::HTTP1, version_));
  }

private:
  Runtime::MockLoader runtime_;
  Ssl::ContextManagerImpl context_manager_{runtime_};
};

INSTANTIATE_TEST_CASE_P(IpVersions, SdsStaticUpstreamIntegrationTest,
                        testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                        TestUtility::ipTestParamsToString);

TEST_P(SdsStaticUpstreamIntegrationTest, RouterRequestAndResponseWithGiantBodyBuffer) {
  testRouterRequestAndResponseWithBody(16 * 1024 * 1024, 16 * 1024 * 1024, false, nullptr);
}

} // namespace Ssl
} // namespace Envoy
