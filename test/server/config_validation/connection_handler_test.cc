#include "envoy/api/api.h"

#include "common/network/address_impl.h"

#include "server/config_validation/connection_handler.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/ssl/mocks.h"
#include "test/mocks/stats/mocks.h"

namespace Envoy {
namespace Server {

using testing::KilledBySignal;
using testing::NiceMock;
using testing::Return;

TEST(ValidationConnectionHandlerDeathTest, MockedMethods) {
  Api::MockApi* api = new Api::MockApi();
  Event::MockDispatcher* dispatcher = new NiceMock<Event::MockDispatcher>();
  EXPECT_CALL(*api, allocateDispatcher_()).WillOnce(Return(dispatcher));

  ValidationConnectionHandler handler(Api::ApiPtr{api});
  EXPECT_EQ(0, handler.numConnections());
  Network::Address::Ipv4Instance address("0.0.0.0", 0);
  EXPECT_EQ(nullptr, handler.findListenerByAddress(address));
  EXPECT_NO_THROW(handler.closeListeners());

  NiceMock<Network::MockFilterChainFactory> filter_factory;
  NiceMock<Network::MockListenSocket> socket;
  NiceMock<Stats::MockStore> scope;
  Network::ListenerOptions options;
  EXPECT_EXIT(handler.addListener(filter_factory, socket, scope, options), KilledBySignal(SIGABRT),
              "not implemented");

  NiceMock<Ssl::MockServerContext> server_context;
  EXPECT_EXIT(handler.addSslListener(filter_factory, server_context, socket, scope, options),
              KilledBySignal(SIGABRT), "not implemented");
}

} // Server
} // Envoy
