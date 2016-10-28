#include "server/configuration_impl.h"

#include "test/mocks/common.h"
#include "test/mocks/network/mocks.h"

using testing::InSequence;
using testing::Return;

namespace Server {
namespace Configuration {

TEST(FilterChainUtility, buildFilterChain) {
  Network::MockConnection connection;
  std::list<NetworkFilterFactoryCb> factories;
  ReadyWatcher watcher;
  NetworkFilterFactoryCb factory = [&](Network::FilterManager&) -> void { watcher.ready(); };
  factories.push_back(factory);
  factories.push_back(factory);

  // Make sure we short circuit if needed.
  InSequence s;
  EXPECT_CALL(connection, state()).WillOnce(Return(Network::Connection::State::Open));
  EXPECT_CALL(watcher, ready());
  EXPECT_CALL(connection, state()).WillOnce(Return(Network::Connection::State::Closing));
  FilterChainUtility::buildFilterChain(connection, factories);
}

} // Configuration
} // Server
