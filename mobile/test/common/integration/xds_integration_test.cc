#include "xds_integration_test.h"

#include "envoy/config/bootstrap/v3/bootstrap.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.h"

#include "test/common/grpc/grpc_client_integration.h"
#include "test/common/integration/base_client_integration_test.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

#include "extension_registry.h"
#include "gtest/gtest.h"

namespace Envoy {

using ::testing::AssertionFailure;
using ::testing::AssertionResult;
using ::testing::AssertionSuccess;

XdsIntegrationTest::XdsIntegrationTest() : BaseClientIntegrationTest(ipVersion()) {}

void XdsIntegrationTest::initialize() {
  create_xds_upstream_ = true;
  tls_xds_upstream_ = true;
  sotw_or_delta_ = sotwOrDelta();

  // Register the extensions required for Envoy Mobile.
  ExtensionRegistry::registerFactories();

  if (sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedSotw ||
      sotw_or_delta_ == Grpc::SotwOrDelta::UnifiedDelta) {
    config_helper_.addRuntimeOverride("envoy.reloadable_features.unified_mux", "true");
  }

  // xDS upstream is created separately in the test infra, and there's only one non-xDS cluster.
  setUpstreamCount(1);

  BaseClientIntegrationTest::initialize();

  default_request_headers_.setScheme("https");
}

Network::Address::IpVersion XdsIntegrationTest::ipVersion() const {
  return std::get<0>(GetParam());
}

Grpc::ClientType XdsIntegrationTest::clientType() const { return std::get<1>(GetParam()); }

Grpc::SotwOrDelta XdsIntegrationTest::sotwOrDelta() const { return std::get<2>(GetParam()); }

void XdsIntegrationTest::SetUp() {
  // TODO(abeyad): Add paramaterized tests for HTTP1, HTTP2, and HTTP3.
  setUpstreamProtocol(Http::CodecType::HTTP2);
}

void XdsIntegrationTest::createEnvoy() {
  BaseClientIntegrationTest::createEnvoy();
  if (on_server_init_function_) {
    on_server_init_function_();
  }
}

void XdsIntegrationTest::initializeXdsStream() {
  createXdsConnection();
  AssertionResult result =
      xds_connection_->waitForNewStream(*BaseIntegrationTest::dispatcher_, xds_stream_);
  RELEASE_ASSERT(result, result.message());
  xds_stream_->startGrpcStream();
}

envoy::config::cluster::v3::Cluster
XdsIntegrationTest::createSingleEndpointClusterConfig(const std::string& cluster_name) {
  envoy::config::cluster::v3::Cluster config;
  config.set_name(cluster_name);

  // Set the endpoint.
  auto* load_assignment = config.mutable_load_assignment();
  load_assignment->set_cluster_name(cluster_name);
  auto* endpoint = load_assignment->add_endpoints()->add_lb_endpoints()->mutable_endpoint();
  endpoint->mutable_address()->mutable_socket_address()->set_address(
      Network::Test::getLoopbackAddressString(ipVersion()));
  endpoint->mutable_address()->mutable_socket_address()->set_port_value(0);

  // Set the protocol options.
  envoy::extensions::upstreams::http::v3::HttpProtocolOptions options;
  options.mutable_explicit_http_config()->mutable_http2_protocol_options();
  (*config.mutable_typed_extension_protocol_options())
      ["envoy.extensions.upstreams.http.v3.HttpProtocolOptions"]
          .PackFrom(options);
  return config;
}

std::string XdsIntegrationTest::getUpstreamCert() {
  return TestEnvironment::readFileToStringForTest(
      TestEnvironment::runfilesPath("test/config/integration/certs/upstreamcacert.pem"));
}

} // namespace Envoy
