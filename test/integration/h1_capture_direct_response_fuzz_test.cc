#include "test/integration/h1_fuzz.h"

namespace Envoy {

void H1FuzzIntegrationTest::initialize() {
  const std::string body = "Response body";
  const std::string file_path = TestEnvironment::writeStringToFileForTest("test_envoy", body);
  const std::string domain("direct.example.com");
  const std::string prefix("/");
  const Http::Code status(Http::Code::OK);
  config_helper_.addConfigModifier(
      [&file_path, &domain, &prefix](
          envoy::config::filter::network::http_connection_manager::v2::HttpConnectionManager& hcm)
          -> void {
        auto* route_config = hcm.mutable_route_config();
        auto* virtual_host = route_config->add_virtual_hosts();
        virtual_host->set_name(domain);
        virtual_host->add_domains(domain);
        virtual_host->add_routes()->mutable_match()->set_prefix(prefix);
        virtual_host->mutable_routes(0)->mutable_direct_response()->set_status(
            static_cast<uint32_t>(status));
        virtual_host->mutable_routes(0)->mutable_direct_response()->mutable_body()->set_filename(
            file_path);
      });
  HttpIntegrationTest::initialize();
}

DEFINE_PROTO_FUZZER(const test::integration::CaptureFuzzTestCase& input) {
  RELEASE_ASSERT(TestEnvironment::getIpVersionsForTest().size() > 0);
  const auto ip_version = TestEnvironment::getIpVersionsForTest()[0];
  H1FuzzIntegrationTest h1_fuzz_integration_test(ip_version);
  h1_fuzz_integration_test.replay(input);
}

} // namespace Envoy
