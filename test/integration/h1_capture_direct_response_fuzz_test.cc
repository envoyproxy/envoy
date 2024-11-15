#include "envoy/extensions/filters/network/http_connection_manager/v3/http_connection_manager.pb.h"

#include "test/integration/h1_fuzz.h"

namespace Envoy {

void H1FuzzIntegrationTest::initialize() {
  const std::string body = "Response body";
  const std::string prefix("/");
  const Http::Code status(Http::Code::OK);
  config_helper_.addConfigModifier(
      [&body, &prefix](
          envoy::extensions::filters::network::http_connection_manager::v3::HttpConnectionManager&
              hcm) -> void {
        auto* route_config = hcm.mutable_route_config();
        // adding direct response mode to the default route
        auto* default_route =
            hcm.mutable_route_config()->mutable_virtual_hosts(0)->mutable_routes(0);
        default_route->mutable_match()->set_prefix(prefix);
        default_route->mutable_direct_response()->set_status(static_cast<uint32_t>(status));
        // Use inline bytes rather than a filename to avoid using a path that may look illegal to
        // Envoy.
        default_route->mutable_direct_response()->mutable_body()->set_inline_bytes(body);
        // adding headers to the default route
        auto* header_value_option = route_config->mutable_response_headers_to_add()->Add();
        header_value_option->mutable_header()->set_value("direct-response-enabled");
        header_value_option->mutable_header()->set_key("x-direct-response-header");
      });
  HttpIntegrationTest::initialize();
}

DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  RELEASE_ASSERT(!TestEnvironment::getIpVersionsForTest().empty(), "");
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  PERSISTENT_FUZZ_VAR(H1FuzzIntegrationTest, h1_fuzz_integration_test, (ip_version));
  h1_fuzz_integration_test.replay(input, true);
}

} // namespace Envoy
