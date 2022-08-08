#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/core/v3/config_source.pb.h"
#include "envoy/extensions/transport_sockets/tls/v3/cert.pb.h"
#include "envoy/service/secret/v3/sds.pb.h"

#include "test/common/integration/xds_integration_test.h"
#include "test/config/integration/certs/clientcert_hash.h"
#include "test/integration/ssl_utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace {

// Hack to force linking of the service: https://github.com/google/protobuf/issues/4221.
const envoy::service::secret::v3::SdsDummy _sds_dummy;

class SdsIntegrationTest : public XdsIntegrationTest {
public:
  SdsIntegrationTest() {
    skip_tag_extraction_rule_check_ = true;
    upstream_tls_ = true;

    config_helper_.addConfigModifier([this](envoy::config::bootstrap::v3::Bootstrap& bootstrap) {
      // Change the base_h2 cluster to use SSL and SDS.
      auto* transport_socket =
          bootstrap.mutable_static_resources()->mutable_clusters(0)->mutable_transport_socket();
      envoy::extensions::transport_sockets::tls::v3::UpstreamTlsContext tls_context;
      tls_context.set_sni("lyft.com");
      auto* secret_config =
          tls_context.mutable_common_tls_context()->add_tls_certificate_sds_secret_configs();
      setUpSdsConfig(secret_config, "client_cert");

      transport_socket->set_name("envoy.transport_sockets.tls");
      transport_socket->mutable_typed_config()->PackFrom(tls_context);
    });
  }

  void createUpstreams() override {
    // This is the fake upstream configured with SSL for the base_h2 cluster.
    addFakeUpstream(Ssl::createUpstreamSslContext(context_manager_, *api_), upstreamProtocol(),
                    /*autonomous_upstream=*/true);
  }

protected:
  void sendSdsResponse(const envoy::extensions::transport_sockets::tls::v3::Secret& secret) {
    envoy::service::discovery::v3::DiscoveryResponse discovery_response;
    discovery_response.set_version_info("1");
    discovery_response.set_type_url(Config::TypeUrl::get().Secret);
    discovery_response.add_resources()->PackFrom(secret);

    xds_stream_->sendGrpcMessage(discovery_response);
  }

  void setUpSdsConfig(envoy::extensions::transport_sockets::tls::v3::SdsSecretConfig* secret_config,
                      const std::string& secret_name) {
    secret_config->set_name(secret_name);
    auto* config_source = secret_config->mutable_sds_config();
    config_source->set_resource_api_version(envoy::config::core::v3::ApiVersion::V3);
    auto* api_config_source = config_source->mutable_api_config_source();
    api_config_source->set_api_type(envoy::config::core::v3::ApiConfigSource::GRPC);
    api_config_source->set_transport_api_version(envoy::config::core::v3::V3);
    auto* grpc_service = api_config_source->add_grpc_services();
    setGrpcService(*grpc_service, std::string(XDS_CLUSTER), fake_upstreams_.back()->localAddress());
  }

  envoy::extensions::transport_sockets::tls::v3::Secret getClientSecret() {
    envoy::extensions::transport_sockets::tls::v3::Secret secret;
    secret.set_name("client_cert");
    auto* tls_certificate = secret.mutable_tls_certificate();
    tls_certificate->mutable_certificate_chain()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientcert.pem"));
    tls_certificate->mutable_private_key()->set_filename(
        TestEnvironment::runfilesPath("test/config/integration/certs/clientkey.pem"));
    return secret;
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersionsClientTypeDelta, SdsIntegrationTest,
                         DELTA_SOTW_GRPC_CLIENT_INTEGRATION_PARAMS);

// Note: Envoy Mobile does not have listener sockets, so we aren't including a downstream test.
TEST_P(SdsIntegrationTest, SdsForUpstreamCluster) {
  on_server_init_function_ = [this]() {
    initializeXdsStream();
    sendSdsResponse(getClientSecret());
  };
  initialize();

  // Wait until the Envoy instance has obtained an updated secret from the SDS cluster. This
  // verifies that the SDS API is working from the Envoy client and allows us to know we can start
  // sending HTTP requests to the upstream cluster using the secret.
  ASSERT_TRUE(waitForCounterGe("sds.client_cert.update_success", 1));
  ASSERT_TRUE(
      waitForCounterGe("cluster.base_h2.client_ssl_socket_factory.ssl_context_update_by_sds", 1));

  stream_->sendHeaders(envoyToMobileHeaders(default_request_headers_), true);
  terminal_callback_.waitReady();

  EXPECT_EQ(cc_.on_headers_calls, 1);
  EXPECT_EQ(cc_.status, "200");
  EXPECT_EQ(cc_.on_complete_calls, 1);
  EXPECT_EQ(cc_.on_cancel_calls, 0);
  EXPECT_EQ(cc_.on_error_calls, 0);
  EXPECT_EQ(cc_.on_header_consumed_bytes_from_response, 13);
}

} // namespace
} // namespace Envoy
