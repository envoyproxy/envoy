#include <memory>
#include <string>

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/discovery/v3/discovery.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "source/common/config/api_version.h"
#include "source/common/event/dispatcher_impl.h"
#include "source/common/http/utility.h"
#include "source/common/network/connection_impl.h"
#include "source/common/network/utility.h"

#ifdef ENVOY_ENABLE_QUIC
#include "source/common/quic/client_connection_factory_impl.h"
#endif

#include "source/common/tls/context_manager_impl.h"
#include "source/common/tls/server_context_config_impl.h"
#include "source/common/tls/server_ssl_socket.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/config/integration/certs/servercert_info.h"
#include "test/config/integration/certs/server2cert_info.h"
#include "test/integration/http_integration.h"
#include "test/integration/server.h"
#include "test/integration/ssl_utility.h"
#include "test/mocks/secret/mocks.h"
#include "test/test_common/network_utility.h"
#include "test/test_common/registry.h"
#include "test/test_common/resources.h"
#include "test/test_common/test_time_system.h"
#include "test/test_common/utility.h"
#include "test/integration/integration.h"

#include "absl/strings/match.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace CertificateSelectors {
namespace OnDemand {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

struct TestParams {
  Grpc::ClientType sds_grpc_type;
};

std::string sdsTestParamsToString(const ::testing::TestParamInfo<TestParams>& p) {
  return fmt::format("{}", p.param.sds_grpc_type == Grpc::ClientType::GoogleGrpc ? "GoogleGrpc"
                                                                                 : "EnvoyGrpc");
}

std::vector<TestParams> getSdsTestsParams() {
  std::vector<TestParams> ret;
  for (auto sds_grpc_type : TestEnvironment::getsGrpcVersionsForTest()) {
    ret.push_back(TestParams{sds_grpc_type});
  }
  return ret;
}

class OnDemandIntegrationTest : public Grpc::BaseGrpcClientIntegrationParamTest,
                                public HttpIntegrationTest,
                                public testing::TestWithParam<TestParams> {
public:
  OnDemandIntegrationTest()
      : HttpIntegrationTest(Http::CodecType::HTTP1, Network::Address::IpVersion::v4,
                            ConfigHelper::httpProxyConfig(false)) {}

  Network::Address::IpVersion ipVersion() const override{
      return Network::Address::IpVersion::v4} Grpc::ClientType clientType() const override {
    return GetParam().sds_grpc_type;
  }
  virtual std::unique_ptr<FakeUpstream>& sdsUpstream() { return fake_upstreams_[0]; }
  // Index in fake_upstreams_ vector of the data plane upstream.
  int dataPlaneUpstreamIndex() { return 1; }

  void initialize() override {
    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      config_helper_.configDownstreamTransportSocketWithTls(
          bootstrap,
          [this](
              envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
            configToUseSds(common_tls_context);
          });
      // The SNI of the certificates loaded in this test.
      default_request_headers_.setHost("www.lyft.com");
    });

    config_helper_.addConfigModifier([](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Add a static SDS cluster as the first cluster in the list.
      // The SDS cluster needs to appear before the cluster that uses it for secrets, so that it
      // gets initialized first.
      bootstrap.mutable_static_resources()->add_clusters()->MergeFrom(
          bootstrap.static_resources().clusters(0));
      auto* sds_cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
      sds_cluster->set_name("sds_cluster");
      sds_cluster->mutable_load_assignment()->set_cluster_name("sds_cluster");
      ConfigHelper::setHttp2(*sds_cluster);
    });

    HttpIntegrationTest::initialize();
    client_ssl_ctx_ = createClientSslTransportSocketFactory({}, context_manager_, *api_);
  }

  void configToUseSds(
      envoy::extensions::transport_sockets::tls::v3::CommonTlsContext& common_tls_context) {
    common_tls_context.add_alpn_protocols(Http::Utility::AlpnNames::get().Http11);

    auto* validation_context = common_tls_context.mutable_validation_context();
    validation_context->mutable_trusted_ca()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/cacert.pem"));
    validation_context->add_verify_certificate_hash(TEST_CLIENT_CERT_HASH);

    // Modify the listener ssl cert to use SDS from sds_cluster
    auto* secret_config_rsa = common_tls_context.add_tls_certificate_sds_secret_configs();
    setUpSdsConfig(secret_config_rsa, server_cert_rsa_);
  }

  void createUpstreams() override {
    // SDS cluster is H2, while the data cluster is H1.
    addFakeUpstream(Http::CodecType::HTTP2);
    addFakeUpstream(Http::CodecType::HTTP1);
    xds_upstream_ = fake_upstreams_.front().get();
  }

  void TearDown() override {
    cleanUpXdsConnection();
    client_ssl_ctx_.reset();
  }

  Network::ClientConnectionPtr makeSslClientConnection() {
    int port = lookupPort("http");
    Network::Address::InstanceConstSharedPtr address = Ssl::getSslAddress(version_, port);
    return dispatcher_->createClientConnection(
        address, Network::Address::InstanceConstSharedPtr(),
        client_ssl_ctx_->createTransportSocket(nullptr, nullptr), nullptr, nullptr);
  }

protected:
  void createSdsStream(FakeUpstream&) {
    createXdsConnection();

    AssertionResult result2 = xds_connection_->waitForNewStream(*dispatcher_, xds_stream_);
    RELEASE_ASSERT(result2, result2.message());
    xds_stream_->startGrpcStream();
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      const std::string& secret_name,
                      const std::string& sds_cluster = "sds_cluster") {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, sds_cluster, sdsUpstream()->localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getServerSecretRsa() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(server_cert_rsa_);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/servercert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/serverkey.pem"));
    return secret;
  }

  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TestTypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);

    xds_stream_->sendGrpcMessage(discovery_response);
  }

  void printServerCounters() {
    std::cerr << "all counters" << std::endl;
    for (const auto& c : test_server_->counters()) {
      std::cerr << "counter: " << c->name() << ", value: " << c->value() << std::endl;
    }
  }

  const std::string server_cert_rsa_{"server_cert_rsa"};
  const std::string server2_cert_rsa_{"server2_cert_rsa"};
  const std::string server_cert_ecdsa_{"server_cert_ecdsa"};
  const std::string validation_secret_{"validation_secret"};
  const std::string client_cert_{"client_cert"};
  Network::UpstreamTransportSocketFactoryPtr client_ssl_ctx_;
};

TEST_P(OnDemandIntegrationTest, BasicSuccess) {
  on_server_init_function_ = [this]() {
    createSdsStream(*sdsUpstream());
    sendSdsResponse(getServerSecretRsa());
  };
  initialize();

  ConnectionCreationFunction creator = [&]() -> Network::ClientConnectionPtr {
    return makeSslClientConnection();
  };
  testRouterHeaderOnlyRequestAndResponse(&creator, dataPlaneUpstreamIndex());
  printServerCounters();

  // Success
  EXPECT_EQ(1, test_server_->counter("sds.server_cert_rsa.update_success")->value());
  EXPECT_EQ(0, test_server_->counter("sds.server_cert_rsa.update_rejected")->value());
}

INSTANTIATE_TEST_SUITE_P(IpVersionsClientType, OnDemandIntegrationTest,
                         testing::ValuesIn(getSdsTestsParams()), sdsTestParamsToString);

} // namespace OnDemand
} // namespace CertificateSelectors
} // namespace Extensions
} // namespace Envoy
