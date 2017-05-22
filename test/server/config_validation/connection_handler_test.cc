#include "server/config_validation/connection_handler.h"

#include "common/network/address_impl.h"

#include "envoy/api/api.h"

#include "test/mocks/api/mocks.h"
#include "test/mocks/event/mocks.h"

namespace Envoy {
namespace Server {

using testing::NiceMock;
using testing::Return;

TEST(ValidationConnectionHandlerTest, MockedMethods) {
  Api::MockApi* api = new Api::MockApi();
  Event::MockDispatcher* dispatcher = new NiceMock<Event::MockDispatcher>();
  EXPECT_CALL(*api, allocateDispatcher_()).WillOnce(Return(dispatcher));

  ValidationConnectionHandler handler(Api::ApiPtr{api});
  EXPECT_EQ(0, handler.numConnections());
  Network::Address::Ipv4Instance address("0.0.0.0", 0);
  EXPECT_EQ(nullptr, handler.findListenerByAddress(address));
  EXPECT_NO_THROW(handler.closeListeners());
}

} // Server
} // Envoy
