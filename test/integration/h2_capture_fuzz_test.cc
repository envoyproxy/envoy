#include "test/integration/h2_fuzz.h"

namespace Envoy {
void H2FuzzIntegrationTest::initialize() {
  config_helper_.addConfigModifier([&](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
    RELEASE_ASSERT(bootstrap.mutable_static_resources()->clusters_size() >= 1, "");
    auto* cluster = bootstrap.mutable_static_resources()->mutable_clusters(0);
    cluster->mutable_http2_protocol_options()->set_allow_metadata(true);
  });
  config_helper_.addConfigModifier(
      [&](envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void { hcm.mutable_http2_protocol_options()->set_allow_metadata(true); });
  setDownstreamProtocol(Http::CodecClient::Type::HTTP2);
  setUpstreamProtocol(FakeHttpConnection::Type::HTTP2);

  HttpIntegrationTest::initialize();
}

DEFINE_PROTO_FUZZER(const test::integration::H2CaptureFuzzTestCase& input) {
  // Pick an IP version to use for loopback, it doesn't matter which.
  FUZZ_ASSERT(!TestEnvironment::getIpVersionsForTest().empty());
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  PERSISTENT_FUZZ_VAR H2FuzzIntegrationTest h2_fuzz_integration_test(ip_version);
  h2_fuzz_integration_test.replay(input, false);
}

} // namespace Envoy
