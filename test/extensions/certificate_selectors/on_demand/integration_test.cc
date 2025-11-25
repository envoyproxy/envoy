#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/fake_upstream.h"
#include "test/integration/integration.h"
#include "test/integration/ssl_utility.h"
#include "test/integration/tcp_proxy_integration.h"
#include "test/test_common/resources.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

class OnDemandIntegrationTest : public TcpProxySslIntegrationTest {
public:
  void TearDown() override { cleanUpXdsConnection(); }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [this](
              envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            configToUseSds(common_tls_context);
          });
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster");
      ConfigHelper::setHttp2(*sds_cluster);
    });
    TcpProxySslIntegrationTest::initialize();
    test_server_->waitUntilListenersReady();
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    auto* validation_context = common_tls_context.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

    // Parse on-demand TLS cert selector config.
    envoy::extensions::certificate_selectors::on_demand_secret::v3::Config on_demand_config;
    TestUtility::loadFromYaml(on_demand_config_, on_demand_config);

    // Configure config source
    auto* config_source = on_demand_config.mutable_config_source();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_timeout()->set_seconds(300);
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
    common_tls_context.mutable_custom_tls_certificate_selector()->set_name("on-demand-config");
    common_tls_context.mutable_custom_tls_certificate_selector()->mutable_typed_config()->PackFrom(
        on_demand_config);
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }
  FakeUpstream* dataStream() override { return fake_upstreams_.back().get(); }

protected:
  void createSdsStream() {
    createXdsConnection();
    AssertionResult result2 = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result2, result2.message());
    xds_stream_->startGrpcStream();
  }

  envoy::extensions::transport_sockets::tls::v3::Secret
  getServerSecretRsa(absl::string_view cert = "server") {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(cert);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
        absl::StrCat("test/config/integration/certs/", cert, "cert.pem")));
    tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
        absl::StrCat("test/config/integration/certs/", cert, "key.pem")));
    return secret;
  }

  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);
    xds_stream_->sendGrpcMessage(discovery_response);
  }

  const std::string server_cert_{"server"};
  const std::string server2_cert_{"server2"};
  std::string on_demand_config_;
};

TEST_P(OnDemandIntegrationTest, BasicSuccessWithPrefetch) {
  on_server_init_function_ = [this]() {
    createSdsStream();
    sendSdsResponse(getServerSecretRsa());
  };
  on_demand_config_ = R"EOF(
  secret_name:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_selectors.on_demand_secret.v3.StaticName
      name: server
  prefetch_names:
  - server
  )EOF";

  setupConnections();
  sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(
      1, test_server_->counter(listenerStatPrefix("on_demand_secret.certificate_added"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessWithoutPrefetch) {
  on_demand_config_ = R"EOF(
  secret_name:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_selectors.on_demand_secret.v3.StaticName
      name: server
  )EOF";
  initialize();

  auto conn = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  createSdsStream();
  sendSdsResponse(getServerSecretRsa());
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(
      1, test_server_->counter(listenerStatPrefix("on_demand_secret.certificate_added"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMixed) {
  on_server_init_function_ = [this]() {
    createSdsStream();
    sendSdsResponse(getServerSecretRsa("server2"));
  };
  on_demand_config_ = R"EOF(
  secret_name:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_selectors.on_demand_secret.v3.StaticName
      name: server
  prefetch_names:
  - server2
  )EOF";
  initialize();

  auto conn = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  sendSdsResponse(getServerSecretRsa());
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();

  EXPECT_EQ(2, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(
      2, test_server_->counter(listenerStatPrefix("on_demand_secret.certificate_added"))->value());
}

TEST_P(OnDemandIntegrationTest, TwoPendingConnections) {
  on_demand_config_ = R"EOF(
  secret_name:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_selectors.on_demand_secret.v3.StaticName
      name: server
  )EOF";
  initialize();

  // Queue two connections in pending state.
  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  auto conn2 = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);

  createSdsStream();
  sendSdsResponse(getServerSecretRsa());

  conn1->waitForUpstreamConnection();
  conn2->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();
  conn2->sendAndReceiveTlsData("lorem", "ipsum");
}

TEST_P(OnDemandIntegrationTest, ClientInterruptedHandshake) {
  on_demand_config_ = R"EOF(
  secret_name:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_selectors.on_demand_secret.v3.StaticName
      name: server
  )EOF";
  initialize();

  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  conn1->ssl_client_->close(Network::ConnectionCloseType::NoFlush);
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  conn1.reset();

  // Request is still outstanding, so we can respond to it, and it will be used later.
  createSdsStream();
  sendSdsResponse(getServerSecretRsa());

  /* TODO
  auto conn2 = std::make_unique<ClientSslConnection>(*this);
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  */
  EXPECT_EQ(
      1, test_server_->counter(listenerStatPrefix("on_demand_secret.certificate_added"))->value());
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
