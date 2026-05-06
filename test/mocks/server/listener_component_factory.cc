#include "listener_component_factory.h"

#include "envoy/config/core/v3/base.pb.h"

#include "test/mocks/server/drain_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;

MockListenerComponentFactory::MockListenerComponentFactory()
    : socket_(std::make_shared<testing::NiceMock<Network::MockListenSocket>>()) {
  ON_CALL(*this, createListenSocket(_, _, _, _, _, _))
      .WillByDefault(Invoke([&](Network::Address::InstanceConstSharedPtr, Network::Socket::Type,
                                const Network::Socket::OptionsSharedPtr& options,
                                ListenerComponentFactory::BindType,
                                const Network::SocketCreationOptions&, uint32_t) {
        if (!Network::Socket::applyOptions(options, *socket_,
                                           envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
          throw EnvoyException("MockListenerComponentFactory: Setting socket options failed");
        }
        return socket_;
      }));
  ON_CALL(*this, createDrainManager_(_))
      .WillByDefault(Invoke([](envoy::config::listener::v3::Listener::DrainType) {
        return new testing::NiceMock<MockDrainManager>();
      }));
}

MockListenerComponentFactory::~MockListenerComponentFactory() = default;

} // namespace Server
} // namespace Envoy
