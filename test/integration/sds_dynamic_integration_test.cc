#include <memory>
#include <string>

#include "envoy/api/v2/discovery.pb.h"
#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "common/config/api_version.h"
#include "common/event/dispatcher_impl.h"
#include "common/http/utility.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"
#include "extensions/transport_sockets/tls/ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/resources.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"

#include "absl/strings/match.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"
#include "integration.h"
#include "utility.h"

namespace Envoy {
namespace Ssl {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

// Sds integration base class with following support:
// * functions to create sds upstream, and send sds response
// * functions to create secret protobuf.
class SdsDynamicIntegrationBaseTest : public Grpc::GrpcClientIntegrationParamTest,
                                      public HttpIntegrationTest {
public:
  SdsDynamicIntegrationBaseTest()
      : HttpIntegrationTest(Http::CodecClient::Type::HTTP1, ipVersion()),
        server_cert_("server_cert"), validation_secret_("validation_secret"),
        client_cert_("client_cert") {}

protected:
  void createSdsStream(FakeUpstream&) {
    createXdsConnection();

    AssertionResult result2 = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result2, result2.message());
    xds_stream_->startGrpcStream();
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      const std::string& secret_name) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "sds_cluster", fake_upstreams_.back()->localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getServerSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(server_cert_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getCvcSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getCvcSecretWithOnlyTrustedCa() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getClientSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(client_cert_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }

  envoy::extensions::transport_sockets::tls::v3::Secret
  getWrongSecret(const std::string& secret_name) {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(secret_name);
    secret.mutable_tls_certificate();
    return secret;
  }

  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    API_NO_BOOST(envoy::api::v2::DiscoveryResponse) discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(API_DOWNGRADE(secret));

    xds_stream_->sendGrpcMessage(discovery_response);
  }

  void PrintServerCounters() {
    std::cerr << "all counters" << std::endl;
    for (const auto& c : test_server_->counters()) {
      std::cerr << "counter: " << c->name() << ", value: " << c->value() << std::endl;
    }
  }

  const std::string server_cert_;
  const std::string validation_secret_;
  const std::string client_cert_;
};

// Downstream SDS integration test: static Listener with ssl cert from SDS
class SdsDynamicDownstreamIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      auto* common_tls_context = tls_context.mutable_common_tls_context();
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();
      common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

      auto* validation_context = common_tls_context->mutable_validation_context();
      validation_context->mutable_trusted_ca()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
      validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

      // Modify the listener ssl cert to use SDS from sds_cluster
      auto* secret_config = common_tls_context->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, "server_cert");

      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);

      // Add a static sds cluster
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_http2_protocol_options();
    });

    HttpIntegrationTest::initialize();
    client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_, *api_);
  }

  void createUpstreams() override {
    create_xds_upstream_ = true;
    HttpIntegrationTest::createUpstreams();
  }

  void TearDown() override {
    cleanUpXdsConnection();
    client_ssl_ctx_.reset();
  }

  Network::ClientConnectionPtr makeSslClientConnection() {
    Network::Address::InstanceConstSharedPtr address = getSslAddress(version_, lookupPort("http"));
    return dispatcher_->createClientConnection(address, Network::Address::InstanceConstSharedPtr(),
                                               client_ssl_ctx_->createTransportSocket(nullptr),
                                               nullptr);
  }

protected:
  Network::TransportSocketFactoryPtr client_ssl_ctx_;
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicDownstreamIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// A test that SDS server send a good server secret for a static listener.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getServerSecret());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

// A test that SDS server send a bad secret for a static listener,
// The first ssl request should fail at connecting.
// then SDS send a good server secret,  the second request should be OK.
TEST_P(SdsDynamicDownstreamIntegrationTest, WrongSecretFirst) {
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getWrongSecret(server_cert_));
  };
  initialize();

  codec_client_ = makeRawHttpConnection(makeSslClientConnection(), absl::nullopt);
  // the connection state is not connected.
  EXPECT_FALSE(codec_client_->connected());
  codec_client_->connection()->close(Network::ConnectionCloseType::NoFlush);

  sendSdsResponse(getServerSecret());

  // Wait for ssl_context_updated_by_sds counter.
  test_server_->waitForCounterGe(
      listenerStatPrefix("server_ssl_socket_factory.ssl_context_update_by_sds"), 1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

class SdsDynamicDownstreamCertValidationContextTest : public SdsDynamicDownstreamIntegrationTest {
public:
  SdsDynamicDownstreamCertValidationContextTest() = default;

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
      auto* common_tls_context = tls_context.mutable_common_tls_context();
      common_tls_context->add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

      auto* tls_certificate = common_tls_context->add_tls_certificates();
      tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
      tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
      setUpSdsValidationContext(common_tls_context);
      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);

      // Add a static sds cluster
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_http2_protocol_options();

      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext upstream_tls_context;
      if (share_validation_secret_) {
        // Configure static cluster with SDS config referencing "validation_secret",
        // which is going to be processed before LDS resources.
        ASSERT(use_lds_);
        setUpSdsValidationContext(upstream_tls_context.mutable_common_tls_context());
      }
      // Enable SSL/TLS with a client certificate in the first cluster.
      auto* upstream_tls_certificate =
          upstream_tls_context.mutable_common_tls_context()->add_tls_certificates();
      upstream_tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
      upstream_tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
      auto* upstream_transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      upstream_transport_socket->set_name("envoy.transport_sockets.tls");
      upstream_transport_socket->mutable_typed_config()->PackFrom(upstream_tls_context);
    });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
    client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_, *api_);
  }

  void setUpSdsValidationContext(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext* common_tls_context) {
    if (use_combined_validation_context_) {
      // Modify the listener context validation type to use combined certificate validation
      // context.
      auto* combined_config = common_tls_context->mutable_combined_validation_context();
      auto* default_validation_context = combined_config->mutable_default_validation_context();
      default_validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
      auto* secret_config = combined_config->mutable_validation_context_sds_secret_config();
      setUpSdsConfig(secret_config, validation_secret_);
    } else {
      // Modify the listener context validation type to use dynamic certificate validation
      // context.
      auto* secret_config = common_tls_context->mutable_validation_context_sds_secret_config();
      setUpSdsConfig(secret_config, validation_secret_);
    }
  }

  void createUpstreams() override {
    // Fake upstream with SSL/TLS for the first cluster.
    fake_upstreams_.emplace_back(new FakeUpstream(
        createUpstreamSslContext(), 0, FakeHttpConnection::Type::HTTP1, version_, timeSystem()));
    create_xds_upstream_ = true;
  }

  Network::TransportSocketFactoryPtr createUpstreamSslContext() {
    envoy::extensions::transport_sockets::tls::v3::DownstreamTlsContext tls_context;
    auto* common_tls_context = tls_context.mutable_common_tls_context();
    auto* tls_certificate = common_tls_context->add_tls_certificates();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));

    auto cfg = std::make_unique<Extensions::TransportSockets::Tls::ServerContextConfigImpl>(
        tls_context, factory_context_);
    static Stats::Scope* upstream_stats_store = new Stats::TestIsolatedStoreImpl();
    return std::make_unique<Extensions::TransportSockets::Tls::ServerSslSocketFactory>(
        std::move(cfg), context_manager_, *upstream_stats_store, std::vector<std::string>{});
  }

  void TearDown() override {
    cleanUpXdsConnection();

    client_ssl_ctx_.reset();
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
  }

  void enableCombinedValidationContext(bool enable) { use_combined_validation_context_ = enable; }
  void shareValidationSecret(bool share) { share_validation_secret_ = share; }

private:
  bool use_combined_validation_context_{false};
  bool share_validation_secret_{false};
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, SdsDynamicDownstreamCertValidationContextTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// A test that SDS server send a good certificate validation context for a static listener.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getCvcSecret());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

// A test that SDS server sends a certificate validation context for a static listener.
// Listener combines default certificate validation context and the dynamic one.
// The first ssl request should be OK.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, CombinedCertValidationContextSuccess) {
  enableCombinedValidationContext(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getCvcSecretWithOnlyTrustedCa());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

// A test that verifies that both: static cluster and LDS listener are updated when using
// the same verification secret (standalone validation context) from the SDS server.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, BasicWithSharedSecret) {
  shareValidationSecret(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getCvcSecret());
  };
  initialize();

  // Wait for "ssl_context_updated_by_sds" counters to indicate that both resources
  // depending on the verification_secret were updated.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  test_server_->waitForCounterGe(
      listenerStatPrefix("server_ssl_socket_factory.ssl_context_update_by_sds"), 1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

// A test that verifies that both: static cluster and LDS listener are updated when using
// the same verification secret (combined validation context) from the SDS server.
TEST_P(SdsDynamicDownstreamCertValidationContextTest, CombinedValidationContextWithSharedSecret) {
  enableCombinedValidationContext(true);
  shareValidationSecret(true);
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getCvcSecretWithOnlyTrustedCa());
  };
  initialize();

  // Wait for "ssl_context_updated_by_sds" counters to indicate that both resources
  // depending on the verification_secret were updated.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);
  test_server_->waitForCounterGe(
      listenerStatPrefix("server_ssl_socket_factory.ssl_context_update_by_sds"), 1);

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator);
}

// Upstream SDS integration test: a static cluster has ssl cert from SDS.
class SdsDynamicUpstreamIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // add sds cluster first.
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_http2_protocol_options();

      // change the first cluster with ssl and sds.
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      auto* secret_config =
          tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, "client_cert");

      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    });

    HttpIntegrationTest::initialize();
    registerTestServerPorts({"http"});
  }

  void TearDown() override {
    cleanUpXdsConnection();

    cleanupUpstreamAndDownstream();
    codec_client_.reset();

    test_server_.reset();
    fake_upstreams_.clear();
  }

  void createUpstreams() override {
    // This is for backend with ssl
    fake_upstreams_.emplace_back(new FakeUpstream(createUpstreamSslContext(context_manager_, *api_),
                                                  0, FakeHttpConnection::Type::HTTP1, version_,
                                                  timeSystem()));
    create_xds_upstream_ = true;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, SdsDynamicUpstreamIntegrationTest,
                         GRPC_CLIENT_INTEGRATION_PARAMS);

// To test a static cluster with sds. SDS send a good client secret first.
// The first request should work.
TEST_P(SdsDynamicUpstreamIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getClientSecret());
  };

  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // There is a race condition here; there are two static clusters:
  // backend cluster_0 with sds and sds_cluster. cluster_0 is created first, its init_manager
  // is called so it issues a sds call, but fail since sds_cluster is not added yet.
  // so cluster_0 is initialized with an empty secret. initialize() will not wait and will return.
  // the testing request will be called, even though in the pre_worker_function, a good sds is
  // send, the cluster will be updated with good secret, the testing request may fail if it is
  // before context is updated. Hence, need to wait for context_update counter.
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);

  testRouterHeaderOnlyRequestAndResponse();
}

// To test a static cluster with sds. SDS send a bad client secret first.
// The first request should fail with 503,  then SDS sends a good client secret,
// the second request should work.
TEST_P(SdsDynamicUpstreamIntegrationTest, WrongSecretFirst) {
  on_server_init_function_ = [this]() {
    createSdsStream(*(fake_upstreams_[1]));
    sendSdsResponse(getWrongSecret(client_cert_));
  };
  initialize();
  fake_upstreams_[0]->set_allow_unexpected_disconnects(true);

  // Make a simple request, should get 503
  BufferingStreamDecoderPtr response = IntegrationUtil::makeSingleRequest(
      lookupPort("http"), "GET", "/test/long/url", "", downstream_protocol_, version_);
  ASSERT_TRUE(response->complete());
  EXPECT_EQ("503", response->headers().getStatusValue());

  // To flush out the reset connection from the first request in upstream.
  FakeRawConnectionPtr fake_upstream_connection;
  ASSERT_TRUE(fake_upstreams_[0]->waitForRawConnection(fake_upstream_connection));
  ASSERT_TRUE(fake_upstream_connection->waitForDisconnect());

  sendSdsResponse(getClientSecret());
  test_server_->waitForCounterGe(
      "cluster.cluster_0.client_ssl_socket_factory.ssl_context_update_by_sds", 1);

  testRouterHeaderOnlyRequestAndResponse();
}

} // namespace Ssl
} // namespace Envoy
