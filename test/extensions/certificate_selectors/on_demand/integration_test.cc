#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/certificate_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/certificate_selectors/on_demand_secret/v3/config.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
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

  void setup(const std::string& on_demand_config) {
    config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [&](envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            configToUseSds(common_tls_context, on_demand_config);
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
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context,
      const std::string& on_demand_config) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    auto* validation_context = common_tls_context.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

    // Parse on-demand TLS cert selector config.
    envoy::extensions::certificate_selectors::on_demand_secret::v3::Config on_demand;
    TestUtility::loadFromYaml(on_demand_config, on_demand);

    // Configure config source
    auto* config_source = on_demand.mutable_config_source();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_timeout()->set_seconds(300);
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
    common_tls_context.mutable_custom_tls_certificate_selector()->set_name("on-demand-config");
    common_tls_context.mutable_custom_tls_certificate_selector()->mutable_typed_config()->PackFrom(
        on_demand);
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  FakeUpstream* dataStream() override { return fake_upstreams_.back().get(); }

protected:
  envoy::extensions::transport_sockets::tls::v3::Secret getServerSecretRsa(absl::string_view cert) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(cert);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
        absl::StrCat("test/config/integration/certs/", cert, "cert.pem")));
    tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
        absl::StrCat("test/config/integration/certs/", cert, "key.pem")));
    return secret;
  }

  void waitSendSdsResponse(absl::string_view cert) {
    RELEASE_ASSERT(!xds_streams_.contains(cert), "Do not call twice");
    FakeStreamPtr xds_stream;
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_stream);
    RELEASE_ASSERT(result, result.message());
    xds_stream->startGrpcStream();

    envoy::service::discovery::v3::DeltaDiscoveryRequest delta_discovery_request;
    AssertionResult result2 = xds_stream->waitForGrpcMessage(*dispatcher_, delta_discovery_request);
    RELEASE_ASSERT(result2, result2.message());
    EXPECT_EQ(1, delta_discovery_request.resource_names_subscribe().size())
        << "Should be 1 resource in DELTA_GRPC";
    EXPECT_EQ(cert, delta_discovery_request.resource_names_subscribe().at(0))
        << "Secret name doesn't match in the request";
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    auto* resource = discovery_response.add_resources();
    resource->set_name(cert);
    resource->mutable_resource()->PackFrom(getServerSecretRsa(cert));
    xds_stream->sendGrpcMessage(discovery_response);
    xds_streams_[cert] = std::move(xds_stream);
  }

  void removeSecret(absl::string_view cert) {
    RELEASE_ASSERT(xds_streams_.contains(cert), "Must have established a stream");
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_removed_resources(cert);
    xds_streams_[cert]->sendGrpcMessage(discovery_response);
  }

  void waitCertsRequested(uint32_t count) {
    test_server_->waitForCounterEq(listenerStatPrefix("on_demand_secret.cert_requested"), count,
                                   TestUtility::DefaultTimeout, dispatcher_.get());
  }

  absl::flat_hash_map<std::string, FakeStreamPtr> xds_streams_;
};

TEST_P(OnDemandIntegrationTest, BasicSuccessWithPrefetch) {
  on_server_init_function_ = [&]() {
    createXdsConnection();
    waitSendSdsResponse("server");
  };
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server
  )EOF");
  auto conn = std::make_unique<ClientSslConnection>(*this);
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(1,
            test_server_->counter(listenerStatPrefix("on_demand_secret.cert_requested"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessWithoutPrefetch) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMixed) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server2
  )EOF");

  createXdsConnection();
  waitSendSdsResponse("server2");
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(2);
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(1, test_server_->counter("sds.server2.update_success")->value());
}

TEST_P(OnDemandIntegrationTest, TwoPendingConnections) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  // Queue two connections in pending state.
  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  auto conn2 = std::make_unique<ClientSslConnection>(*this);
  dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn1->waitForUpstreamConnection();
  conn2->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();
  conn2->sendAndReceiveTlsData("lorem", "ipsum");
}

TEST_P(OnDemandIntegrationTest, ClientInterruptedHandshake) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  conn1->ssl_client_->close(Network::ConnectionCloseType::NoFlush);
  conn1.reset();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, ConnectTimeout) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* connect_timeout = filter_chain->mutable_transport_socket_connect_timeout();
    connect_timeout->set_seconds(1);
    connect_timeout->set_nanos(0);
  });
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  auto conn = std::make_unique<ClientSslConnection>(*this);
  test_server_->waitForCounterEq(
      listenerStatPrefix("downstream_cx_transport_socket_connect_timeout"), 1,
      TestUtility::DefaultTimeout, dispatcher_.get());
  conn->ssl_client_->close(Network::ConnectionCloseType::NoFlush);
  conn.reset();
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, SecretRemoved) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.certificate_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  removeSecret("server");
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
