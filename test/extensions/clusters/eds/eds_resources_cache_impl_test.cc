#include "source/extensions/clusters/eds/eds_resources_cache_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Upstream {

class EdsResourcesCacheImplTest : public testing::Test {
public:
  using ResourceType = envoy::config::endpoint::v3::ClusterLoadAssignment;

  EdsResourcesCacheImplTest() {
    TestUtility::loadFromYaml(R"EOF(
      resource_api_version: V3
      api_config_source:
        api_type: DELTA_GRPC
        transport_api_version: V3
        grpc_services:
          envoy_grpc:
            cluster_name: xds_cluster
      )EOF",
                              default_config_source_);
  }

  // An implementation of EdsResourceRemovalCallback that tracks calls with using a counter.
  struct ResourceRemovalCallbackCounter : public EdsResourceRemovalCallback {
    void onCachedResourceRemoved(absl::string_view resource_name) override {
      auto& resource_calls_counter = calls_counter_map_[resource_name];
      resource_calls_counter++;
    }

    absl::flat_hash_map<std::string, uint32_t> calls_counter_map_;
  };

  envoy::config::core::v3::ConfigSource default_config_source_;

  EdsResourcesCacheImpl cache_;
};

// Validates that simple addition works.
TEST_F(EdsResourcesCacheImplTest, AddSuccess) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());

  // Add a resource.
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());
}

// Validates that simple addition and update works.
TEST_F(EdsResourcesCacheImplTest, AddAndSetSuccess) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());

  // Add a resource.
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Add a new resource.
  ResourceType new_resource;
  new_resource.set_cluster_name("new_foo");
  resources_cache.setResource("new_foo_cla", new_resource);
  EXPECT_EQ(2, resources_cache.cacheSizeForTest());

  // Update the first resource.
  ResourceType resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache.setResource("foo_cla", resource_update);
  EXPECT_EQ(2, resources_cache.cacheSizeForTest());
}

// Validates simple addition and fetching works.
TEST_F(EdsResourcesCacheImplTest, AddAndFetchSuccess) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);

  const auto& fetched_resource = resources_cache.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
}

// Validates simple addition, update and fetching works.
TEST_F(EdsResourcesCacheImplTest, AddUpdateAndFetchSuccess) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);

  const auto& fetched_resource = resources_cache.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());

  // Update the resource.
  ResourceType resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache.setResource("foo_cla", resource_update);

  const auto& fetched_resource2 = resources_cache.getResource("foo_cla", nullptr);
  EXPECT_TRUE(fetched_resource2.has_value());
  EXPECT_EQ("foo_update", fetched_resource2->cluster_name());
}

// Validates that adding and fetching a different resource returns nullopt.
TEST_F(EdsResourcesCacheImplTest, AddAndFetchNonExistent) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Fetch non-existent resource name.
  const auto& fetched_resource = resources_cache.getResource("non_existent", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that fetching from an empty map returns nullopt.
TEST_F(EdsResourcesCacheImplTest, FetchEmpty) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
  const auto& fetched_resource = resources_cache.getResource("foo_cla", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that adding and removing works.
TEST_F(EdsResourcesCacheImplTest, AddRemoveThenFetchEmpty) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Remove the resource.
  resources_cache.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());

  // Fetch the removed resource name.
  const auto& fetched_resource = resources_cache.getResource("foo_cla", nullptr);
  EXPECT_FALSE(fetched_resource.has_value());
}

// Validates that adding and removing a non-existent resource works.
TEST_F(EdsResourcesCacheImplTest, AddAndRemoveNonExistent) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Remove the resource.
  resources_cache.removeResource("non_existent");
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());
}

// Validates that the removal from an empty map works.
TEST_F(EdsResourcesCacheImplTest, RemoveEmpty) {
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
  resources_cache.removeResource("non_existent");
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
}

// Validate removal callback gets notified.
TEST_F(EdsResourcesCacheImplTest, RemoveCallbackCalled) {
  ResourceRemovalCallbackCounter callback;

  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Remove the resource.
  resources_cache.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
}

// Validate removal callback isn't invoked after update.
TEST_F(EdsResourcesCacheImplTest, RemoveCallbackNotCalledAfterUpdate) {
  ResourceRemovalCallbackCounter callback;

  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Update the resource.
  ResourceType resource_update;
  resource_update.set_cluster_name("foo_update");
  resources_cache.setResource("foo_cla", resource_update);

  // Remove the resource.
  resources_cache.removeResource("foo_cla");
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);
}

// Validate correct removal callback gets notified when multiple resources are used.
TEST_F(EdsResourcesCacheImplTest, MultipleResourcesRemoveCallbackCalled) {
  ResourceRemovalCallbackCounter callback;
  auto& resources_cache = cache_.getConfigSourceResourceMap(default_config_source_);

  // Add first resource.
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource = resources_cache.getResource("foo_cla", &callback);
  EXPECT_TRUE(fetched_resource.has_value());
  EXPECT_EQ("foo", fetched_resource->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo_cla"]);

  // Add second resource.
  ResourceType resource2;
  resource2.set_cluster_name("foo2");
  resources_cache.setResource("foo2_cla", resource2);
  EXPECT_EQ(2, resources_cache.cacheSizeForTest());

  // Fetch the resource and register a callback.
  const auto& fetched_resource2 = resources_cache.getResource("foo2_cla", &callback);
  EXPECT_TRUE(fetched_resource2.has_value());
  EXPECT_EQ("foo2", fetched_resource2->cluster_name());
  EXPECT_EQ(0, callback.calls_counter_map_["foo2_cla"]);

  // Remove the first resource.
  resources_cache.removeResource("foo_cla");
  EXPECT_EQ(1, resources_cache.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
  EXPECT_EQ(0, callback.calls_counter_map_["foo2_cla"]);

  // Remove the second resource.
  resources_cache.removeResource("foo2_cla");
  EXPECT_EQ(0, resources_cache.cacheSizeForTest());
  EXPECT_EQ(1, callback.calls_counter_map_["foo_cla"]);
  EXPECT_EQ(1, callback.calls_counter_map_["foo2_cla"]);
}

// Validate first-level cache is different depending on the ConfigSource.
TEST_F(EdsResourcesCacheImplTest, SameConfigSourceCache) {
  auto& resources_cache1 = cache_.getConfigSourceResourceMap(default_config_source_);
  ResourceType resource;
  resource.set_cluster_name("foo");
  resources_cache1.setResource("foo_cla", resource);
  EXPECT_EQ(1, resources_cache1.cacheSizeForTest());

  // Use cache with the same ConfigSource.
  auto& resources_cache_same = cache_.getConfigSourceResourceMap(default_config_source_);
  EXPECT_EQ(1, resources_cache_same.cacheSizeForTest());

  // Use cache with a different ConfigSource.
  envoy::config::core::v3::ConfigSource config_source2 = default_config_source_;
  config_source2.mutable_api_config_source()
      ->mutable_grpc_services(0)
      ->mutable_envoy_grpc()
      ->set_cluster_name("xds_cluster2");
  auto& resources_cache_different = cache_.getConfigSourceResourceMap(config_source2);
  EXPECT_EQ(0, resources_cache_different.cacheSizeForTest());
}

} // namespace Upstream
} // namespace Envoy
