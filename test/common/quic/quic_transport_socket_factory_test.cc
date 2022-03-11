#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::ReturnRef;

namespace Envoy {
namespace Quic {

class QuicServerTransportSocketFactoryConfigTest : public Event::TestUsingSimulatedTime,
                                                   public testing::Test {
public:
  QuicServerTransportSocketFactoryConfigTest()
      : server_api_(Api::createApiForTest(server_stats_store_, simTime())) {
    ON_CALL(context_, api()).WillByDefault(ReturnRef(*server_api_));
  }

  void verifyQuicServerTransportSocketFactory(std::string yaml, bool expect_early_data) {
    envoy::extensions::transport_sockets::quic::v3::QuicDownstreamTransport proto_config;
    TestUtility::loadFromYaml(yaml, proto_config);
    Network::TransportSocketFactoryPtr transport_socket_factory =
        config_factory_.createTransportSocketFactory(proto_config, context_, {});
    EXPECT_EQ(expect_early_data,
              static_cast<QuicServerTransportSocketFactory&>(*transport_socket_factory)
                  .earlyDataEnabled());
  }

  QuicServerTransportSocketConfigFactory config_factory_;
  Stats::TestUtil::TestStore server_stats_store_;
  Api::ApiPtr server_api_;
  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
};

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataEnabledByDefault) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, true);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataExplicitlyDisabled) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
enable_early_data:
  value: false
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, false);
}

TEST_F(QuicServerTransportSocketFactoryConfigTest, EarlyDataExplicitlyEnabled) {
  const std::string yaml = TestEnvironment::substitute(R"EOF(
downstream_tls_context:
  common_tls_context:
    tls_certificates:
    - certificate_chain:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_cert.pem"
      private_key:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/san_uri_key.pem"
    validation_context:
      trusted_ca:
        filename: "{{ test_rundir }}/test/extensions/transport_sockets/tls/test_data/ca_cert.pem"
enable_early_data:
  value: true
)EOF");

  verifyQuicServerTransportSocketFactory(yaml, true);
}

} // namespace Quic
} // namespace Envoy
