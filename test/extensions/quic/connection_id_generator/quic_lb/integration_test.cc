#include "envoy/extensions/quic/connection_id_generator/quic_lb/v3/quic_lb.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "test/integration/http_integration.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionIdGenerator {
namespace QuicLb {

class QuicLbIntegrationTest : public testing::TestWithParam<Network::Address::IpVersion>,
                              public HttpIntegrationTest {
public:
  QuicLbIntegrationTest() : HttpIntegrationTest(Http::CodecType::HTTP3, GetParam()) {
    TestEnvironment::writeStringToFileForTest("quic_lb.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: quic_lb
    generic_secret:
      secrets:
        key:
          inline_string: "0000000000000000"
        version:
          inline_bytes: 1
)EOF",
                                              false);

    config_helper_.addConfigModifier(
        [=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto connection_id_config = listener->mutable_udp_listener_config()
                                          ->mutable_quic_options()
                                          ->mutable_connection_id_generator_config();

          envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config config;
          config.set_unsafe_unencrypted_testing_mode(true);
          *config.mutable_server_id()->mutable_inline_string() = "myid";
          config.set_nonce_length_bytes(12);
          auto* sds = config.mutable_encryption_parmeters();
          sds->set_name("quic_lb");
          sds->mutable_sds_config()->mutable_path_config_source()->set_path(
              TestEnvironment::substitute("{{ test_tmpdir }}/quic_lb.yaml"));
          connection_id_config->mutable_typed_config()->PackFrom(config);
          connection_id_config->set_name("envoy.quic.connection_id_generator.quic_lb");

          listener->set_stat_prefix("test");
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicLbIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicLbIntegrationTest, Basic) { testRouterHeaderOnlyRequestAndResponse(); }

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
