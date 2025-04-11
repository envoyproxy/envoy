#include "test/mocks/event/mocks.h"
#include "test/mocks/network/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"

#include "contrib/reverse_connection/bootstrap/test/mocks.h"
#include "contrib/reverse_connection/reverse_connection_listener_config/source/active_reverse_connection_listener.h"

using testing::_;
using testing::Invoke;
using testing::NiceMock;
using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace ReverseConnection {
namespace {

using RCThreadLocalRegistry = Extensions::Bootstrap::ReverseConnection::RCThreadLocalRegistry;
using MockReverseConnectionManager =
    Extensions::Bootstrap::ReverseConnection::MockReverseConnectionManager;

// class TestRCThreadLocalRegistry : public RCThreadLocalRegistry {
// public:
//   TestRCThreadLocalRegistry(NiceMock<MockReverseConnectionManager>& manager,
//                             NiceMock<Event::MockDispatcher>& dispatcher, Stats::Scope& scope,
//                             NiceMock<Upstream::MockClusterManager>& cluster_manager)
//       : RCThreadLocalRegistry(dispatcher, scope, "test_prefix", cluster_manager),
//         mock_rc_manager_(manager) {
//   }

//   NiceMock<MockReverseConnectionManager>& getRCManager() {
//     return mock_rc_manager_;
//   }

// private:
//   NiceMock<MockReverseConnectionManager>& mock_rc_manager_;
// };

class ActiveReverseConnectionListenerTest : public testing::Test {
public:
  ActiveReverseConnectionListenerTest() {
    // Set up listener properties
    static const std::string listener_name = "test_listener";
    const uint64_t listener_tag = 1;
    static const std::string version_info = "v1";

    ON_CALL(listener_config_, name()).WillByDefault(ReturnRef(listener_name));
    ON_CALL(listener_config_, listenerTag()).WillByDefault(Return(listener_tag));
    ON_CALL(listener_config_, versionInfo()).WillByDefault(ReturnRef(version_info));
    ON_CALL(conn_handler_, statPrefix()).WillByDefault(ReturnRef(listener_stat_prefix_));
    ON_CALL(listener_config_, listenerScope()).WillByDefault(ReturnRef(*base_scope_));
  }

  void SetUp() override {
    base_scope_ = Stats::ScopeSharedPtr(stats_store.createScope("test."));
    rc_manager_ = std::make_shared<NiceMock<MockReverseConnectionManager>>();
    local_rev_conn_registry_ = std::make_shared<RCThreadLocalRegistry>(rc_manager_, nullptr);
    ENVOY_LOG_MISC(error, "SetUp");
    ON_CALL(reverse_conn_registry_, getLocalRegistry())
        .WillByDefault(Return(local_rev_conn_registry_.get()));
    // ON_CALL(local_rev_conn_registry_, getRCManager()).WillByDefault(ReturnRef(rc_manager_));
    ENVOY_LOG_MISC(error, "SetUp done");
  }

  void addListener() {
    ENVOY_LOG_MISC(error, "addListener");
    mock_listener_ = std::make_unique<NiceMock<Network::MockListener>>();
    ON_CALL(listener_config_, listenerFiltersTimeout())
        .WillByDefault(Return(std::chrono::milliseconds(15000)));
    ON_CALL(listener_config_, continueOnListenerFiltersTimeout()).WillByDefault(Return(false));
    reverse_conn_listener_ = std::make_shared<ActiveReverseConnectionListener>(
        conn_handler_, dispatcher_, std::move(mock_listener_), listener_config_,
        *local_rev_conn_registry_);
    ENVOY_LOG_MISC(error, "addListener done");
  }

  void TearDown() override {
    ENVOY_LOG_MISC(error, "TearDown");
    base_scope_.reset();
  }

  std::string listener_stat_prefix_{"listener_stat_prefix"};
  NiceMock<Network::MockListenerConfig> listener_config_;
  NiceMock<Network::MockRevConnRegistry> reverse_conn_registry_;
  std::shared_ptr<RCThreadLocalRegistry> local_rev_conn_registry_;
  std::shared_ptr<NiceMock<MockReverseConnectionManager>> rc_manager_;
  std::unique_ptr<Network::ReverseConnectionListenerConfig> reverse_connection_listener_config_;
  std::shared_ptr<ActiveReverseConnectionListener> reverse_conn_listener_;
  NiceMock<Event::MockDispatcher> dispatcher_{"test"};
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  NiceMock<Network::MockConnectionHandler> conn_handler_;
  std::unique_ptr<NiceMock<Network::MockListener>> mock_listener_;
  Stats::ScopeSharedPtr base_scope_;
  Stats::IsolatedStoreImpl stats_store;
};

TEST_F(ActiveReverseConnectionListenerTest, ActiveRCListenerTriggersRCWorkflow) {
  EXPECT_CALL(*rc_manager_, registerRCInitiators(_, _));
  addListener();
}

} // namespace
} // namespace ReverseConnection
} // namespace Extensions
} // namespace Envoy
