#include "envoy/extensions/quic/connection_id_generator/quic_lb/v3/quic_lb.pb.h"
#include "envoy/extensions/transport_sockets/quic/v3/quic_transport.pb.h"

#include "source/common/common/base64.h"

#include "test/integration/http_integration.h"
#include "test/integration/quic_http_integration_test.h"

namespace Envoy {
namespace Extensions {
namespace Quic {
namespace ConnectionIdGenerator {
namespace QuicLb {

class QuicLbIntegrationTest : public Envoy::Quic::QuicHttpMultiAddressesIntegrationTest {
public:
  QuicLbIntegrationTest() {
    char version = 1;
    TestEnvironment::writeStringToFileForTest(
        "quic_lb.yaml",
        absl::StrCat(R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: quic_lb
    generic_secret:
      secrets:
        encryption_key:
          inline_string: "0000000000000000"
        configuration_version:
          inline_bytes: )EOF",
                     Base64::encode(&version, sizeof(version)), "\n"),
        false);

    config_helper_.addConfigModifier(
        [=](envoy::config::bootstrap::v3::Bootstrap& bootstrap) -> void {
          auto listener = bootstrap.mutable_static_resources()->mutable_listeners(0);
          auto connection_id_config = listener->mutable_udp_listener_config()
                                          ->mutable_quic_options()
                                          ->mutable_connection_id_generator_config();

          envoy::extensions::quic::connection_id_generator::quic_lb::v3::Config config;
          *config.mutable_server_id()->mutable_inline_string() = "myid";
          config.set_nonce_length_bytes(12);
          auto* sds = config.mutable_encryption_parameters();
          sds->set_name("quic_lb");
          sds->mutable_sds_config()->mutable_path_config_source()->set_path(
              TestEnvironment::substitute("{{ test_tmpdir }}/quic_lb.yaml"));
          connection_id_config->mutable_typed_config()->PackFrom(config);
          connection_id_config->set_name("envoy.quic.connection_id_generator.quic_lb");
        });
  }
};

INSTANTIATE_TEST_SUITE_P(IpVersions, QuicLbIntegrationTest,
                         testing::ValuesIn(TestEnvironment::getIpVersionsForTest()),
                         TestUtility::ipTestParamsToString);

TEST_P(QuicLbIntegrationTest, Basic) { testRouterHeaderOnlyRequestAndResponse(); }

TEST_P(QuicLbIntegrationTest, SecretsNotReady) {
  // Write an invalid secret. This will cause the init manager to complete because it found
  // the file, but won't get loaded because it's invalid.
  TestEnvironment::writeStringToFileForTest("quic_lb.yaml", R"EOF(
resources:
  - "@type": "type.googleapis.com/envoy.extensions.transport_sockets.tls.v3.Secret"
    name: quic_lb
    generic_secret:
      secret: {}
)EOF",
                                            false);

  testRouterHeaderOnlyRequestAndResponse();
}

TEST_P(QuicLbIntegrationTest, MultipleQuicConnections) {
  testMultipleQuicConnections([](size_t index) {
    auto id = quic::test::TestConnectionId(index);

    // Stamp the worker id into the last byte of the CID, so that this connection ID generator
    // will spread connections equally between workers.
    reinterpret_cast<uint8_t*>(id.mutable_data())[id.length() - 1] = index;
    return id;
  });
}

} // namespace QuicLb
} // namespace ConnectionIdGenerator
} // namespace Quic
} // namespace Extensions
} // namespace Envoy
