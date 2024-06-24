#include "source/extensions/config_subscription/grpc/eds_resources_cache_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/test_common/simulated_time_system.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Config {

class EdsResourcesCacheImplTest : public testing::Test {
public:
  using ClusterLoadAssignment = envoy::config::endpoint::v3::ClusterLoadAssignment;

  EdsResourcesCacheImplTest()
      : api_(Api::createApiForTest(time_system_)),
        dispatcher_(api_->allocateDispatcher("test_thread")), resources_cache_(*dispatcher_.get()) {
  }

  // An implementation of EdsResourceRemovalCallback that tracks calls with using a counter.
  struct ResourceRemovalCallbackCounter : public EdsResourceRemovalCallback {
    void onCachedResourceRemoved(absl::string_view resource_name) override {
      auto& resource_calls_counter = calls_counter_map_[resource_name];
      resource_calls_counter++;
    }

    absl::flat_hash_map<std::string, uint32_t> calls_counter_map_;
  };

  Event::SimulatedTimeSystemHelper time_system_;
  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  EdsResourcesCacheImpl resources_cache_;
};

// Validates that simple addition works.
TEST_F(EdsResourcesCacheImplTest, AddSuccess) {
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());

  // Add a resource.
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());
}

// Validates that simple addition and update works.
TEST_F(EdsResourcesCacheImplTest, AddAndSetSuccess) {
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());

  // Add a resource.
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Add a new resource.
  ClusterLoadAssignment new_resource;
  new_resource.set_cluster_name("new_foo");
  resources_cache_.setResource("new_foo_cla", new_resource);
  EXPECT_EQ(2, resources_cache_.cacheSizeForTest());

  // Update the first resource.
  ClusterLoadAssignment resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache_.setResource("foo_cla", resource_update);
  EXPECT_EQ(2, resources_cache_.cacheSizeForTest());
}

// Validates simple addition and fetching works.
TEST_F(EdsResourcesCacheImplTest, AddAndFetchSuccess) {
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);

  const auto& fetched_resource = resources_cache_.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
}

// Validates simple addition, update and fetching works.
TEST_F(EdsResourcesCacheImplTest, AddUpdateAndFetchSuccess) {
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);

  const auto& fetched_resource = resources_cache_.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());

  // Update the resource.
  ClusterLoadAssignment resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache_.setResource("foo_cla", resource_update);

  const auto& fetched_resource2 = resources_cache_.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource2.has_value());
  EXPECT_EQ("foo_update", fetched_resource2->cluster_name());
}

// Validates that adding and fetching a different resource returns nullopt.
TEST_F(EdsResourcesCacheImplTest, AddAndFetchNonExistent) {
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Fetch non-existent resource name.
  const auto& fetched_resource = resources_cache_.getResource("non_existent", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that fetching from an empty map returns nullopt.
TEST_F(EdsResourcesCacheImplTest, FetchEmpty) {
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that adding and removing works.
TEST_F(EdsResourcesCacheImplTest, AddRemoveThenFetchEmpty) {
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Remove the resource.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());

  // Fetch the removed resource name.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that adding and removing a non-existent resource works.
TEST_F(EdsResourcesCacheImplTest, AddAndRemoveNonExistent) {
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Remove the resource.
  resources_cache_.removeResource("non_existent");
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());
}

// Validates that the removal from an empty map works.
TEST_F(EdsResourcesCacheImplTest, RemoveEmpty) {
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  resources_cache_.removeResource("non_existent");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
}

// Validate removal callback gets notified.
TEST_F(EdsResourcesCacheImplTest, RemoveCallbackCalled) {
  ResourceRemovalCallbackCounter callback;

  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Remove the resource.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
}

// Validate removal callback isn't invoked after update.
TEST_F(EdsResourcesCacheImplTest, RemoveCallbackNotCalledAfterUpdate) {
  ResourceRemovalCallbackCounter callback;

  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Update the resource.
  ClusterLoadAssignment resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache_.setResource("foo_cla", resource_update);

  // Remove the resource.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);
}

// Validate explicit callback removal does not get notification.
TEST_F(EdsResourcesCacheImplTest, ExplicitRemoveCallback) {
  ResourceRemovalCallbackCounter callback;

  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Remove the callback.
  resources_cache_.removeCallback("foo_cla", &callback);

  // Remove the resource, callback not invoked.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);
}

// Validate explicit callback removal of multiple callbacks with the same name,
// and a call to setResource in between is executed properly.
TEST_F(EdsResourcesCacheImplTest, ExplicitSameNameRemoveCallbacks) {
  ResourceRemovalCallbackCounter callback1;
  ResourceRemovalCallbackCounter callback2;

  // Emulate receiving a resource.
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  // Set the CLA resource to some resource_name.
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Emulate resource fetched from cache with 2 different callbacks.
  // Fetch the resource by the first name and register a callback.
  const auto& fetched_resource1 = resources_cache_.getResource("foo_cla", &callback1);
  EXPECT_TRUE(fetched_resource1.has_value());
  EXPECT_EQ("foo", fetched_resource1->cluster_name());
  EXPECT_EQ(0, callback1.calls_counter_map_["foo_cla"]);
  // Fetch the resource by the second name and register the same callback.
  const auto& fetched_resource2 = resources_cache_.getResource("foo_cla", &callback2);
  EXPECT_TRUE(fetched_resource2.has_value());
  EXPECT_EQ("foo", fetched_resource2->cluster_name());
  EXPECT_EQ(0, callback2.calls_counter_map_["foo_cla"]);

  // Emulate receiving the resource again, and invoking the callbacks removal.
  resources_cache_.removeCallback("foo_cla", &callback1);

  // Set the resource again (an ongoing update).
  ClusterLoadAssignment resource2;
  resource2.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource2);

  // Remove the callback using the second name.
  resources_cache_.removeCallback("foo_cla", &callback2);

  // Remove the resource using the first name, callback not invoked.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(0, callback1.calls_counter_map_["foo_cla"]);
  EXPECT_EQ(0, callback2.calls_counter_map_["foo_cla"]);
}

// Validate correct removal callback gets notified when multiple resources are used.
TEST_F(EdsResourcesCacheImplTest, MultipleResourcesRemoveCallbackCalled) {
  ResourceRemovalCallbackCounter callback;

  // Add first resource.
  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Add second resource.
  ClusterLoadAssignment resource2;
  resource2.set_cluster_name("foo2");
  resources_cache_.setResource("foo2_cla", resource2);
  EXPECT_EQ(2, resources_cache_.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource2 = resources_cache_.getResource("foo2_cla", &callback);
  EXPECT_TRUE(fetched_resource2.has_value());
  EXPECT_EQ("foo2", fetched_resource2->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo2_cla"]);

  // Remove the first resource.
  resources_cache_.removeResource("foo_cla");
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
  EXPECT_EQ(0, callback.calls_counter_map_["foo2_cla"]);

  // Remove the second resource.
  resources_cache_.removeResource("foo2_cla");
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
  EXPECT_EQ(1, callback.calls_counter_map_["foo2_cla"]);
}

// Validate that adding a timer and expiring it invokes a notification.
TEST_F(EdsResourcesCacheImplTest, ExpiredCacheResource) {
  ResourceRemovalCallbackCounter callback;

  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Set an expiration timer for the resource.
  resources_cache_.setExpiryTimer("foo_cla", std::chrono::seconds(1));

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Make sure the expiration is removing the resource and invoking the callback.
  time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                 Envoy::Event::Dispatcher::RunType::Block);
  EXPECT_EQ(0, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
}

// Validate that adding a timer with an expiration timer, and disabling the timer
// prevents invoking a notification.
TEST_F(EdsResourcesCacheImplTest, DisableExpiredCacheResource) {
  ResourceRemovalCallbackCounter callback;

  ClusterLoadAssignment resource;
  resource.set_cluster_name("foo");
  resources_cache_.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());

  // Set an expiration timer for the resource.
  resources_cache_.setExpiryTimer("foo_cla", std::chrono::seconds(1));

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache_.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Disable the timer
  resources_cache_.disableExpiryTimer("foo_cla");

  // Make sure the expiration isn't invoking the callback.
  time_system_.advanceTimeAndRun(std::chrono::seconds(1), *dispatcher_,
                                 Envoy::Event::Dispatcher::RunType::Block);
  EXPECT_EQ(1, resources_cache_.cacheSizeForTest());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);
}

} // namespace Config
} // namespace Envoy
