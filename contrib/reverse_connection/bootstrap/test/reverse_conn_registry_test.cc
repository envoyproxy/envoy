#include "test/mocks/event/mocks.h"
#include "test/mocks/stats/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/mocks/upstream/cluster_manager.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_global_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_conn_thread_local_registry.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_handler.h"
#include "contrib/reverse_connection/bootstrap/source/reverse_connection_manager.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::Return;
using testing::NiceMock;
using testing::ReturnRef;
using testing::Invoke;

namespace Envoy {
namespace Extensions {
namespace Bootstrap {
namespace ReverseConnection {
namespace {

class ReverseConnRegistryTest : public testing::Test {
public:
  ReverseConnRegistryTest() {
    reverse_conn_registry_ = std::make_shared<ReverseConnRegistry>();
  }

  void SetUp() override {
    ENVOY_LOG_MISC(error, "SetUp");
    base_scope_ = Stats::ScopeSharedPtr(stats_store.createScope("rc_initiator."));
  }

  void initializeSlot() {
    EXPECT_CALL(tls_, allocateSlot());
    rc_thread_local_registry_ = std::make_shared<RCThreadLocalRegistry>(dispatcher_, *base_scope_, "test_prefix", cluster_manager_);
    tls_slot_ =
      ThreadLocal::TypedSlot<Bootstrap::ReverseConnection::RCThreadLocalRegistry>::makeUnique(tls_);
    tls_.setDispatcher(&dispatcher_);
    tls_slot_->set([registry = rc_thread_local_registry_](Event::Dispatcher&) { return registry; });
    reverse_conn_registry_->tls_slot_ = std::move(tls_slot_);
  }

  void TearDown() override {
    ENVOY_LOG_MISC(error, "TearDown");
    tls_slot_.reset();  // Explicitly de-allocate all entities using the scope before the scope is destroyed.
    rc_thread_local_registry_.reset();
    reverse_conn_registry_.reset();
    base_scope_.reset();
  }

  std::unique_ptr<ThreadLocal::TypedSlot<RCThreadLocalRegistry>> tls_slot_;
  std::shared_ptr<RCThreadLocalRegistry> rc_thread_local_registry_;
  NiceMock<ThreadLocal::MockInstance> tls_;
  ThreadLocal::SlotPtr slot_;
  std::shared_ptr<ReverseConnRegistry> reverse_conn_registry_;
  NiceMock<Event::MockDispatcher> dispatcher_;
  NiceMock<Upstream::MockClusterManager> cluster_manager_;
  Stats::ScopeSharedPtr base_scope_;
  Stats::IsolatedStoreImpl stats_store;
};

TEST_F(ReverseConnRegistryTest, TlsSlotNotInitialized) {
  // Test when tls_slot_ is not initialized.
  reverse_conn_registry_->tls_slot_ = nullptr;
  EXPECT_EQ(reverse_conn_registry_->getLocalRegistry(), nullptr);
}

TEST_F(ReverseConnRegistryTest, TlsSlotSet) {
  initializeSlot(); // Set up the slot.
  EXPECT_NE(reverse_conn_registry_->getLocalRegistry(), nullptr);

  // Verify that the RCManager and RCHandler are set successfully.
  auto local_registry = reverse_conn_registry_->getLocalRegistry();
  EXPECT_NE(&local_registry->getRCManager(), nullptr);
  EXPECT_NE(&local_registry->getRCHandler(), nullptr);
}


TEST_F(ReverseConnRegistryTest, ParseInvalidConfigUrl) {
  // Test with invalid config
  google::protobuf::Any invalid_config;
  invalid_config.set_type_url("invalid_type_url");
  auto status_or_config = reverse_conn_registry_->fromAnyConfig(invalid_config);
  EXPECT_FALSE(status_or_config.ok());
  EXPECT_EQ(status_or_config.status().message(), "Failed to unpack reverse connection listener config");
}

TEST_F(ReverseConnRegistryTest, ParseConfigWithMissingFields) {
  envoy::extensions::reverse_connection::reverse_connection_listener_config::v3alpha::ReverseConnectionListenerConfig config;

  // Case 1: Missing source node ID.
  config.set_src_node_id("");  // Leave source node ID empty.
  config.set_src_cluster_id("test_cluster_id");
  config.set_src_tenant_id("test_tenant_id");

  google::protobuf::Any packed_config;
  packed_config.PackFrom(config);

  auto result = reverse_conn_registry_->fromAnyConfig(packed_config);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(), "Source node ID is missing in reverse connection listener config");

  // Case 2: Missing remote cluster to connection count map.
  config.set_src_node_id("test_src_node_id");
  config.mutable_remote_cluster_to_conn_count()->Clear();  // Leave map empty.

  packed_config.Clear();
  packed_config.PackFrom(config);

  result = reverse_conn_registry_->fromAnyConfig(packed_config);
  EXPECT_FALSE(result.ok());
  EXPECT_EQ(result.status().message(),
            "Remote cluster to connection count map is missing in reverse connection listener config");

  // Case 3: Valid configuration for sanity check (optional).
  config.set_src_node_id("test_src_node_id");
  auto* cluster_count = config.add_remote_cluster_to_conn_count();
  cluster_count->set_cluster_name("test_remote_cluster");
  cluster_count->mutable_reverse_connection_count()->set_value(5);

  packed_config.Clear();
  packed_config.PackFrom(config);

  result = reverse_conn_registry_->fromAnyConfig(packed_config);
  EXPECT_TRUE(result.ok());  // Expect success.
  EXPECT_NE(result.value(), nullptr);  // Ensure we get a valid object.
}

TEST_F(ReverseConnRegistryTest, ParseValidConfig) {
  envoy::extensions::reverse_connection::reverse_connection_listener_config::v3alpha::ReverseConnectionListenerConfig config;
  config.set_src_node_id("test_src_node_id");
  config.set_src_cluster_id("test_cluster_id");
  config.set_src_tenant_id("test_tenant_id");
  auto* cluster_count = config.add_remote_cluster_to_conn_count();
  cluster_count->set_cluster_name("test_remote_cluster");
  cluster_count->mutable_reverse_connection_count()->set_value(5);

  google::protobuf::Any packed_config;
  packed_config.PackFrom(config);

  auto result = reverse_conn_registry_->fromAnyConfig(packed_config);
  EXPECT_TRUE(result.ok());
  EXPECT_NE(result.value(), nullptr);  // Ensure we get a valid ReverseConnectionListenerConfigPtr object.
  EXPECT_EQ(result.value()->getReverseConnParams()->src_node_id_, "test_src_node_id");
}

} // namespace
} // namespace ReverseConnection
} // namespace Bootstrap
} // namespace Extensions
} // namespace Envoy