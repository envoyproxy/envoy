#include "common/network/address_impl.h"

#include "server/config_validation/dispatcher.h"

#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"

namespace Envoy {
namespace Event {

using testing::KilledBySignal;
using testing::NiceMock;

TEST(ValidationDispatcherTest, UnimplementedMethods) {
  ValidationDispatcher dispatcher;
  Network::Address::InstanceConstSharedPtr address{
      new Network::Address::Ipv4Instance("0.0.0.0", 0)};
  EXPECT_EXIT(dispatcher.createClientConnection(address), KilledBySignal(SIGABRT),
              "not implemented");
  NiceMock<Ssl::MockClientContext> client_context;
  EXPECT_EXIT(dispatcher.createSslClientConnection(client_context, address),
              KilledBySignal(SIGABRT), "not implemented");
  EXPECT_EXIT(dispatcher.createDnsResolver(), KilledBySignal(SIGABRT), "not implemented");
  NiceMock<Network::MockConnectionHandler> handler;
  NiceMock<Network::MockListenSocket> socket;
  NiceMock<Network::MockListenerCallbacks> callbacks;
  NiceMock<Stats::MockStore> scope;
  Network::ListenerOptions options;
  EXPECT_EXIT(dispatcher.createListener(handler, socket, callbacks, scope, options),
              KilledBySignal(SIGABRT), "not implemented");
  NiceMock<Ssl::MockServerContext> server_context;
  EXPECT_EXIT(
      dispatcher.createSslListener(handler, server_context, socket, callbacks, scope, options),
      KilledBySignal(SIGABRT), "not implemented");
}

} // Event
} // Envoy
