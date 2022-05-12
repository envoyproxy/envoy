#include "listener_component_factory.h"

#include "envoy/config/core/v3/base.pb.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Server {

using ::testing::_;
using ::testing::Invoke;

MockProdListenerComponentFactory::MockProdListenerComponentFactory(
    NiceMock<Server::MockInstance>& server)
    : ProdListenerComponentFactory(server),
      socket_(std::make_shared<testing::NiceMock<Network::MockListenSocket>>()) {
  ON_CALL(*this, createListenSocket(_, _, _, _, _, _))
      .WillByDefault(Invoke([&](Network::Address::InstanceConstSharedPtr, Network::Socket::Type,
                                const Network::Socket::OptionsSharedPtr& options,
                                ListenerComponentFactory::BindType,
                                const Network::SocketCreationOptions&,
                                uint32_t) -> Network::SocketSharedPtr {
        if (!Network::Socket::applyOptions(options, *socket_,
                                           envoy::config::core::v3::SocketOption::STATE_PREBIND)) {
          throw EnvoyException("MockProdListenerComponentFactory: Setting socket options failed");
        }
        return socket_;
      }));
}

MockProdListenerComponentFactory::~MockProdListenerComponentFactory() = default;

} // namespace Server
} // namespace Envoy
