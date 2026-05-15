#include "source/common/config/decoded_resource_impl.h"
#include "source/common/config/singleton_subscription_adapter.h"

#include "test/mocks/config/mocks.h"
#include "test/test_common/utility.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::NiceMock;
using testing::Ref;
using testing::Return;

namespace Envoy {
namespace Config {
namespace {

class MockSingletonSubscriptionCallbacks : public SingletonSubscriptionCallbacks {
public:
  MOCK_METHOD(absl::Status, onResourceUpdate,
              (const DecodedResource& resource, const std::string& version_info));
  MOCK_METHOD(void, onResourceRemoved, ());
  MOCK_METHOD(void, onFailure, (ConfigUpdateFailureReason reason, const EnvoyException* e));
};

TEST(SingletonSubscriptionAdapterTest, SotWEmptyResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter.onConfigUpdate({}, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, SotWWithResource) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  auto message = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource(std::move(message), "my_resource", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter.onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaRemovedResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("my_resource");

  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter.onConfigUpdate({}, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaAddedResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  auto message = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource(std::move(message), "my_resource", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter.onConfigUpdate(added_resources, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, SotWMultipleResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  auto message1 = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource1(std::move(message1), "my_resource1", {}, "v1");
  auto message2 = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource2(std::move(message2), "my_resource2", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource1, decoded_resource2};

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  EXPECT_FALSE(adapter.onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaMultipleResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  auto message1 = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource1(std::move(message1), "my_resource1", {}, "v1");
  auto message2 = std::make_unique<Protobuf::Empty>();
  DecodedResourceImpl decoded_resource2(std::move(message2), "my_resource2", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource1, decoded_resource2};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  EXPECT_FALSE(adapter.onConfigUpdate(added_resources, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, OnConfigUpdateFailed) {
  MockSingletonSubscriptionCallbacks callbacks;
  SingletonSubscriptionCallbacksAdapter adapter(callbacks);

  EnvoyException ex("test exception");
  EXPECT_CALL(callbacks, onFailure(ConfigUpdateFailureReason::ConnectionFailure, &ex));
  adapter.onConfigUpdateFailed(ConfigUpdateFailureReason::ConnectionFailure, &ex);
}

TEST(SingletonSubscriptionImplTest, Start) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = std::make_unique<SingletonSubscriptionCallbacksAdapter>(callbacks);
  auto sub = std::make_unique<NiceMock<MockSubscription>>();
  MockSubscription* raw_sub = sub.get();

  SingletonSubscriptionImpl singleton_sub(std::move(sub), "my_resource", std::move(adapter));

  EXPECT_CALL(*raw_sub, start(absl::flat_hash_set<std::string>{"my_resource"}));
  singleton_sub.start();
}

} // namespace
} // namespace Config
} // namespace Envoy
