#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/filters/network/tcp_proxy/v3/tcp_proxy.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_mappers/static_name/v3/config.pb.h"
#include "envoy/extensions/transport_sockets/tls/cert_selectors/on_demand_secret/v3/config.pb.h"
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
namespace TransportSockets {
namespace Tls {
namespace CertificateSelectors {
namespace OnDemand {
namespace {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

class OnDemandIntegrationTest : public TcpProxySslIntegrationTest {
public:
  void TearDown() override { cleanUpXdsConnection(); }

  void setup(const std::string& config = "") {
    const std::string on_demand_config = config.empty() ? defaultConfig() : config;
    config_helper_.addConfigModifier(
        [this, on_demand_config](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
          auto* filter_chain =
              bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
          auto* transport_socket = filter_chain->mutable_transport_socket();
          transport_socket->set_name("envoy.transport_sockets.tls");
          envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
          configToUseSds(*tls_context.mutable_common_tls_context(), on_demand_config);
          tls_context.set_disable_stateless_session_resumption(true);
          tls_context.set_disable_stateful_session_resumption(true);
          tls_context.mutable_require_client_certificate()->set_value(mtls_);
          transport_socket->mutable_typed_config()->PackFrom(tls_context);

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

  std::string defaultConfig() const {
    return R"EOF(
      certificate_mapper:
        name: static-name
        typed_config:
          "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
          name: server
      )EOF";
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context,
      const std::string& on_demand_config) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    if (validation_sds_) {
      auto* validation_context = common_tls_context.mutable_validation_context_sds_secret_config();
      validation_context->set_name("cacert");
      setConfigSource(validation_context->mutable_sds_config());
    } else {
      auto* validation_context = common_tls_context.mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
    }

    // Parse on-demand TLS cert selector config.
    envoy::extensions::transport_sockets::tls::cert_selectors::on_demand_secret::v3::Config
        on_demand;
    TestUtility::loadFromYaml(on_demand_config, on_demand);

    // Configure config source
    setConfigSource(on_demand.mutable_config_source());
    common_tls_context.mutable_custom_tls_certificate_selector()->set_name("on-demand-config");
    common_tls_context.mutable_custom_tls_certificate_selector()->mutable_typed_config()->PackFrom(
        on_demand);
  }

  void setConfigSource(envoy::config::core::v3::ConfigSource* config_source) {
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::DELTA_GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    grpc_service->mutable_timeout()->set_seconds(300);
    grpc_service->mutable_envoy_grpc()->set_cluster_name("sds_cluster");
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  FakeUpstream* dataStream() override { return fake_upstreams_.back().get(); }

protected:
  bool mtls_{false};
  bool validation_sds_{false};

  envoy::extensions::transport_sockets::tls::v3::Secret makeSecret(absl::string_view name,
                                                                   absl::string_view cert) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(name);
    if (cert == "cacert" || cert == "upstreamcacert") {
      auto* validation_context = secret.mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, ".pem")));
    } else {
      auto* tls_certificate = secret.mutable_tls_certificate();
      tls_certificate->mutable_certificate_chain()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "cert.pem")));
      tls_certificate->mutable_private_key()->set_filename(TestEnvironment::runfilesPath(
          absl::StrCat("test/config/integration/certs/", cert, "key.pem")));
    }
    return secret;
  }

  FakeStream& waitSendSdsResponse(absl::string_view name, absl::string_view cert = "",
                                  bool fail_fast = false) {
    xds_streams_.emplace_back();
    AssertionResult result = xds_connection_->waitForNewStream(*dispatcher_, xds_streams_.back());
    RELEASE_ASSERT(result, result.message());
    auto& xds_stream = *xds_streams_.back();
    xds_stream.startGrpcStream();

    envoy::service::discovery::v3::DeltaDiscoveryRequest delta_discovery_request;
    AssertionResult result2 = xds_stream.waitForGrpcMessage(*dispatcher_, delta_discovery_request);
    RELEASE_ASSERT(result2, result2.message());
    EXPECT_EQ(1, delta_discovery_request.resource_names_subscribe().size())
        << "Should be 1 resource in DELTA_GRPC";
    EXPECT_EQ(name, delta_discovery_request.resource_names_subscribe().at(0))
        << "Secret name doesn't match in the request";
    if (fail_fast) {
      removeSecret(xds_stream, name);
      return xds_stream;
    }
    sendSecret(xds_stream, name, cert.empty() ? name : cert);
    return xds_stream;
  }

  void sendSecret(FakeStream& xds_stream, absl::string_view name, absl::string_view cert) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    auto* resource = discovery_response.add_resources();
    resource->set_name(name);
    resource->mutable_resource()->PackFrom(makeSecret(name, cert));
    xds_stream.sendGrpcMessage(discovery_response);
  }

  void removeSecret(FakeStream& xds_stream, absl::string_view name) {
    envoy::service::discovery::v3::DeltaDiscoveryResponse discovery_response;
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_removed_resources(name);
    xds_stream.sendGrpcMessage(discovery_response);
    xds_stream.finishGrpcStream(Grpc::Status::Ok);
  }

  void waitCertsRequested(uint32_t count) {
    test_server_->waitForCounterEq(onDemandStat("cert_requested"), count,
                                   TestUtility::DefaultTimeout, dispatcher_.get());
  }

  std::string onDemandStat(absl::string_view stat) {
    return listenerStatPrefix(absl::StrCat("on_demand_secret.", stat));
  }

  std::vector<FakeStreamPtr> xds_streams_;
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
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  prefetch_secret_names:
  - server
  )EOF");
  // Open two connections sequentially.
  for (int i = 0; i < 2; i++) {
    auto conn = std::make_unique<ClientSslConnection>(*this);
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_updated"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessWithoutPrefetch) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
      name: server
  )EOF");
  {
    auto conn = std::make_unique<ClientSslConnection>(*this);
    waitCertsRequested(1);
    createXdsConnection();
    waitSendSdsResponse("server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }

  {
    // Open a second connection, without expecting SDS.
    auto conn2 = std::make_unique<ClientSslConnection>(*this);
    conn2->waitForUpstreamConnection();
    conn2->sendAndReceiveTlsData("hello", "world");
    conn2.reset();
  }
  EXPECT_EQ(1, test_server_->counter("sds.server.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server.update_rejected")->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_requested"))->value());
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_updated"))->value());
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessSNI) {
  ssl_options_.setSni("server");
  setup(R"EOF(
  certificate_mapper:
    name: sni
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.sni.v3.SNI
      default_value: "*"
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
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMixed) {
  setup(R"EOF(
  certificate_mapper:
    name: static-name
    typed_config:
      "@type": type.googleapis.com/envoy.extensions.transport_sockets.tls.cert_mappers.static_name.v3.StaticName
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
  EXPECT_EQ(2, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicFail) {
  setup();
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server", "", true);
  while (!conn->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  test_server_->waitForGaugeEq(onDemandStat("cert_active"), 0);
}

TEST_P(OnDemandIntegrationTest, TwoPendingConnections) {
  setup();
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
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, ClientInterruptedHandshake) {
  setup();
  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  conn1->ssl_client_->close(Network::ConnectionCloseType::NoFlush);
  conn1.reset();
  dispatcher_->run(Event::Dispatcher::RunType::Block);
  // SDS request is still outstanding, so we can respond to it, and it will be used later.
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
  createXdsConnection();
  waitSendSdsResponse("server");
}

TEST_P(OnDemandIntegrationTest, ConnectTimeout) {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    auto* filter_chain =
        bootstrap.mutable_static_resources()->mutable_listeners(0)->mutable_filter_chains(0);
    auto* connect_timeout = filter_chain->mutable_transport_socket_connect_timeout();
    connect_timeout->set_seconds(1);
  });
  setup();
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

TEST_P(OnDemandIntegrationTest, SecretAddRemove) {
  setup();
  // Add successfully.
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());

  // Remove.
  removeSecret(stream, "server");
  test_server_->waitForGaugeEq(onDemandStat("cert_active"), 0);

  // Request again.
  auto conn2 = std::make_unique<ClientSslConnection>(*this);
  waitSendSdsResponse("server");
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, SecretUpdate) {
  setup();
  auto conn1 = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  auto& stream = waitSendSdsResponse("server");
  conn1->waitForUpstreamConnection();
  conn1->sendAndReceiveTlsData("hello", "world");
  conn1.reset();

  // Update with another valid secret.
  sendSecret(stream, "server", "server2");
  test_server_->waitForCounterEq(onDemandStat("cert_updated"), 2);

  auto conn2 = std::make_unique<ClientSslConnection>(*this);
  conn2->waitForUpstreamConnection();
  conn2->sendAndReceiveTlsData("hello", "world");
  conn2.reset();

  EXPECT_EQ(1, test_server_->gauge(onDemandStat("cert_active"))->value());
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsSuccess) {
  mtls_ = true;
  setup();
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

TEST_P(OnDemandIntegrationTest, BasicSuccessMtlsFail) {
  mtls_ = true;
  ssl_options_.no_cert_ = true;
  useListenerAccessLog("%DOWNSTREAM_TRANSPORT_FAILURE_REASON%");
  setup();
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  createXdsConnection();
  waitSendSdsResponse("server");
  while (!conn->connect_callbacks_.closed()) {
    dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
  }
  auto log_result = waitForAccessLog(listener_access_log_name_);
  EXPECT_THAT(log_result, ::testing::HasSubstr("PEER_DID_NOT_RETURN_A_CERTIFICATE"));
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdate) {
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is an invalid CA cert.
    ca_stream = &waitSendSdsResponse("cacert");
  };
  setup();
  // Connection should work as-expected.
  {
    auto conn = std::make_unique<ClientSslConnection>(*this);
    waitCertsRequested(1);
    waitSendSdsResponse("server");
    conn->waitForUpstreamConnection();
    conn->sendAndReceiveTlsData("hello", "world");
    conn.reset();
  }
  EXPECT_EQ(1, test_server_->counter(onDemandStat("cert_updated"))->value());

  // Send a wrong CA via validation SDS and open a new connection that fails.
  {
    sendSecret(*ca_stream, "cacert", "upstreamcacert");
    test_server_->waitForCounterEq(onDemandStat("cert_updated"), 2);
    auto conn = std::make_unique<ClientSslConnection>(*this);
    while (!conn->connect_callbacks_.closed()) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
    conn.reset();
  }
}

TEST_P(OnDemandIntegrationTest, ValidationContextUpdateWithPending) {
  mtls_ = true;
  validation_sds_ = true;
  FakeStream* ca_stream = nullptr;
  on_server_init_function_ = [&]() {
    createXdsConnection();
    // This is an invalid CA cert.
    ca_stream = &waitSendSdsResponse("cacert", "upstreamcacert");
  };
  setup();
  // Queue a pending connection, then issue a context config update, and unblock the connection.
  // In this case, the original context might reference an older context config.
  auto conn = std::make_unique<ClientSslConnection>(*this);
  waitCertsRequested(1);
  // Fix the CA cert, then send the actual server cert.
  sendSecret(*ca_stream, "cacert", "cacert");
  waitSendSdsResponse("server");
  conn->waitForUpstreamConnection();
  conn->sendAndReceiveTlsData("hello", "world");
  conn.reset();
}

INSTANTIATE_TEST_SUITE_P(TcpProxyIntegrationTestParams, OnDemandIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

} // namespace
} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Tls
} // namespace TransportSockets
} // namespace Extensions
} // namespace Envoy
