#include <memory>
#include <string>

#include "envoy/service/discovery/v2/sds.pb.h"

#include "common/config/resources.h"
#include "common/event/dispatcher_impl.h"
#include "common/network/connection_impl.h"
#include "common/network/utility.h"

#include "extensions/transport_sockets/tls/context_config_impl.h"
#include "extensions/transport_sockets/tls/context_manager_impl.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/test_common/network_utility.h"
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
const envoy::service::discovery::v2::SdsDummy _sds_dummy;

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

  void setUpSdsConfig(envoy::api::v2::auth::SdsSecretConfig* secret_config,
                      const std::string& secret_name) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::api::v2::core::ApiConfigSource::GRPC);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, "sds_cluster", fake_upstreams_.back()->localAddress());
  }

  envoy::api::v2::auth::Secret getServerSecret() {
    envoy::api::v2::auth::Secret secret;
    secret.set_name(server_cert_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    return secret;
  }

  envoy::api::v2::auth::Secret getCvcSecret() {
    envoy::api::v2::auth::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);
    return secret;
  }

  envoy::api::v2::auth::Secret getCvcSecretWithOnlyTrustedCa() {
    envoy::api::v2::auth::Secret secret;
    secret.set_name(validation_secret_);
    auto* validation_context = secret.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    return secret;
  }

  envoy::api::v2::auth::Secret getClientSecret() {
    envoy::api::v2::auth::Secret secret;
    secret.set_name(client_cert_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }

  envoy::api::v2::auth::Secret getWrongSecret(const std::string& secret_name) {
    envoy::api::v2::auth::Secret secret;
    secret.set_name(secret_name);
    secret.mutable_tls_certificate();
    return secret;
  }

  void sendSdsResponse(const envoy::api::v2::auth::Secret& secret) {
    envoy::api::v2::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);

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
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      envoy::api::v2::auth::DownstreamTlsContext tls_context;
      auto* common_tls_context = tls_context.mutable_common_tls_context();
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();
      common_tls_context->add_alpn_protocols("http/1.1");

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
    cleanupUpstreamAndDownstream();
    codec_client_.reset();
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

  codec_client_ = makeRawHttpConnection(makeSslClientConnection());
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
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      auto* transport_socket = bootstrap.mutable_static_resources()
                                   ->mutable_listeners(0)
                                   ->mutable_filter_chains(0)
                                   ->mutable_transport_socket();
      envoy::api::v2::auth::DownstreamTlsContext tls_context;
      auto* common_tls_context = tls_context.mutable_common_tls_context();
      common_tls_context->add_alpn_protocols("http/1.1");

      auto* tls_certificate = common_tls_context->add_tls_certificates();
      tls_certificate->mutable_certificate_chain()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
      tls_certificate->mutable_private_key()->set_filename(
          TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));

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

  void enableCombinedValidationContext(bool enable) { use_combined_validation_context_ = enable; }

private:
  bool use_combined_validation_context_{false};
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

// Upstream SDS integration test: a static cluster has ssl cert from SDS.
class SdsDynamicUpstreamIntegrationTest : public SdsDynamicIntegrationBaseTest {
public:
  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v2::Bootstrap& bootstrap) {
      // add sds cluster first.
      auto* sds_cluster = bootstrap.mutable_static_resources()->add_clusters();
      sds_cluster->MergeFrom(bootstrap.static_resources().clusters()[0]);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_http2_protocol_options();

      // change the first cluster with ssl and sds.
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      envoy::api::v2::auth::UpstreamTlsContext tls_context;
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
  EXPECT_EQ("503", response->headers().Status()->value().getStringView());

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
