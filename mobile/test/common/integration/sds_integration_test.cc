#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/integration/ssl_utility.h"
#include "test/test_common/environment.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;
constexpr absl::string_view XDS_CLUSTER_NAME = "test_cluster";
constexpr absl::string_view SECRET_NAME = "client_cert";

class SdsIntegrationTest : public XdsIntegrationTest {
public:
  void initialize() override {
    XdsIntegrationTest::initialize();
    initializeXdsStream();
  }

  void createEnvoy() override {
    const std::string target_uri = Network::Test::getLoopbackAddressUrlString(ipVersion());
    Platform::XdsBuilder xds_builder(target_uri,
                                     fake_upstreams_.back()->localAddress()->ip()->port());
    xds_builder.addClusterDiscoveryService().setSslRootCerts(getUpstreamCert());
    builder_.setXds(std::move(xds_builder));
    XdsIntegrationTest::createEnvoy();
  }

  void SetUp() override { initialize(); }

protected:
  void sendCdsResponse() {
    auto cds_cluster = createSingleEndpointClusterConfig(std::string(XDS_CLUSTER_NAME));
    // Update the cluster to use SSL.
    envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
    tls_context.set_sni("lyft.com");
    auto* secret_config =
        tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
    setUpSdsConfig(secret_config, SECRET_NAME);
    auto* transport_socket = cds_cluster.mutable_transport_socket();
    transport_socket->set_name("envoy.transport_sockets.tls");
    transport_socket->mutable_typed_config()->PackFrom(tls_context);
    sendDiscoveryResponse<envoy::config::cluster::v3::Cluster>(
        Config::TypeUrl::get().Cluster, {cds_cluster}, {cds_cluster}, {}, "55");
  }

  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);
    xds_stream_->sendGrpcMessage(discovery_response);
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      absl::string_view secret_name) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    // Envoy Mobile only supports SDS with ADS.
    config_source->mutable_ads();
    config_source->mutable_initial_fetch_timeout()->set_seconds(5);
  }

  static envoy::extensions::transport_sockets::tls::v3::Secret getClientSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name(SECRET_NAME);
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }
};

INSTANTIATE_TEST_SUITE_P(
    IpVersionsClientTypeSotw, SdsIntegrationTest,
    testing::Combine(testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                     testing::ValuesIn(TestEnvironment::getsGrpcVersionsForTest()),
                     // Envoy Mobile's xDS APIs only support state-of-the-world, not delta.
                     testing::Values(Grpc::SotwOrDelta::Sotw, Grpc::SotwOrDelta::UnifiedSotw)));

// Note: Envoy Mobile does not have listener sockets, so we aren't including a downstream test.
TEST_P(SdsIntegrationTest, SdsForUpstreamCluster) {
  // Wait until the new cluster from CDS is added before sending the SDS response.
  sendCdsResponse();
  ASSERT_TRUE(waitForCounterGe("cluster_manager.cluster_added", 1));

  // Wait until the Envoy instance has obtained an updated secret from the SDS cluster. This
  // verifies that the SDS API is working from the Envoy client and allows us to know we can start
  // sending HTTP requests to the upstream cluster using the secret.
  sendSdsResponse(getClientSecret());
  ASSERT_TRUE(waitForCounterGe(fmt::format("sds.{}.update_success", SECRET_NAME), 1));
  ASSERT_TRUE(
      waitForCounterGe(fmt::format("cluster.{}.client_ssl_socket_factory.ssl_context_update_by_sds",
                                   XDS_CLUSTER_NAME),
                       1));
}

} // namespace
} // namespace Envoy
