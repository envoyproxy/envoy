#include "common/config/utility.h"

#include "extensions/quic_listeners/quiche/active_quic_listener.h"
#include "extensions/quic_listeners/quiche/active_quic_listener_config.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Quic {

class ActiveQuicListenerFactoryPeer {
public:
  static quic::QuicConfig& quicConfig(ActiveQuicListenerFactory& factory) {
    return factory.quic_config_;
  }
};

TEST(ActiveQuicListenerConfigTest, CreateActiveQuicListenerFactory) {
  std::string listener_name = QuicListenerName;
  auto& config_factory =
      Config::Utility::getAndCheckFactoryByName<Server::ActiveUdpListenerConfigFactory>(
          listener_name);
  ProtobufTypes::MessagePtr config = config_factory.createEmptyConfigProto();

  std::string yaml = R"EOF(
    max_concurrent_streams: 10
    idle_timeout: {
      seconds: 2
    }
  )EOF";
  TestUtility::loadFromYaml(yaml, *config);
  Network::ActiveUdpListenerFactoryPtr listener_factory =
      config_factory.createActiveUdpListenerFactory(*config, /*concurrency=*/1);
  EXPECT_NE(nullptr, listener_factory);
  quic::QuicConfig& quic_config = ActiveQuicListenerFactoryPeer::quicConfig(
      dynamic_cast<ActiveQuicListenerFactory&>(*listener_factory));
  EXPECT_EQ(10u, quic_config.GetMaxBidirectionalStreamsToSend());
  EXPECT_EQ(10u, quic_config.GetMaxUnidirectionalStreamsToSend());
  EXPECT_EQ(2000u, quic_config.IdleNetworkTimeout().ToMilliseconds());
  // Default value if not present in config.
  EXPECT_EQ(20000u, quic_config.max_time_before_crypto_handshake().ToMilliseconds());
}

} // namespace Quic
} // namespace Envoy
