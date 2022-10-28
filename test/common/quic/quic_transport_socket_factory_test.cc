#include "source/common/quic/quic_transport_socket_factory.h"

#include "test/mocks/server/transport_socket_factory_context.h"
#include "test/mocks/ssl/mocks.h"
#include "test/test_common/environment.h"
#include "test/test_common/utility.h"

using testing::NiceMock;
using testing::Return;
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
    Network::DownstreamTransportSocketFactoryPtr transport_socket_factory =
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

class QuicClientTransportSocketFactoryTest : public testing::Test {
public:
  QuicClientTransportSocketFactoryTest() {
    EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _)).WillOnce(Return(nullptr));
    EXPECT_CALL(*context_config_, setSecretUpdateCallback(_))
        .WillOnce(testing::SaveArg<0>(&update_callback_));
    factory_.emplace(std::unique_ptr<Envoy::Ssl::ClientContextConfig>(context_config_), context_);
    factory_->initialize();
  }

  NiceMock<Server::Configuration::MockTransportSocketFactoryContext> context_;
  absl::optional<Quic::QuicClientTransportSocketFactory> factory_;
  // Will be owned by factory_.
  NiceMock<Ssl::MockClientContextConfig>* context_config_{
      new NiceMock<Ssl::MockClientContextConfig>};
  std::function<void()> update_callback_;
};

TEST_F(QuicClientTransportSocketFactoryTest, GetCryptoConfig) {
  EXPECT_EQ(nullptr, factory_->getCryptoConfig());

  Ssl::ClientContextSharedPtr ssl_context1{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context1));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config1 = factory_->getCryptoConfig();
  EXPECT_NE(nullptr, crypto_config1);

  Ssl::ClientContextSharedPtr ssl_context2{new Ssl::MockClientContext()};
  EXPECT_CALL(context_.context_manager_, createSslClientContext(_, _))
      .WillOnce(Return(ssl_context2));
  update_callback_();
  std::shared_ptr<quic::QuicCryptoClientConfig> crypto_config2 = factory_->getCryptoConfig();
  EXPECT_NE(crypto_config2, crypto_config1);
}

} // namespace Quic
} // namespace Envoy
