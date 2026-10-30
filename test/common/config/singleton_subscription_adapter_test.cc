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

TEST(SingletonSubscriptionAdapterTest, CreateInvalidResourceName) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter_or_error =
      SingletonSubscriptionCallbacksAdapter::create(callbacks, "xdstp://invalid");
  EXPECT_FALSE(adapter_or_error.ok());
  EXPECT_EQ(adapter_or_error.status().code(), absl::StatusCode::kInvalidArgument);
}

TEST(SingletonSubscriptionAdapterTest, SotWEmptyResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter->onConfigUpdate({}, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, SotWWithResource) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(std::move(message), "my_resource", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, SotWUnexpectedResourceName) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(std::move(message), "wrong_resource", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  auto status = adapter->onConfigUpdate(resources, "v1");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "Unexpected resource name 'wrong_resource' (expecting 'my_resource') in singleton SotW "
            "update");
}

TEST(SingletonSubscriptionAdapterTest, DeltaRemovedResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("my_resource");

  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter->onConfigUpdate({}, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaAddedResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(std::move(message), "my_resource", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(added_resources, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaUnexpectedAddedResourceName) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(std::move(message), "wrong_resource", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  auto status = adapter->onConfigUpdate(added_resources, removed_resources, "v1");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "Unexpected resource name 'wrong_resource' (expecting 'my_resource') in singleton "
            "Delta update");
}

TEST(SingletonSubscriptionAdapterTest, DeltaUnexpectedRemovedResourceName) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("wrong_resource");

  EXPECT_CALL(callbacks, onResourceRemoved()).Times(0);
  auto status = adapter->onConfigUpdate({}, removed_resources, "v1");
  EXPECT_FALSE(status.ok());
  EXPECT_EQ(status.code(), absl::StatusCode::kInvalidArgument);
  EXPECT_EQ(status.message(),
            "Unexpected removed resource name 'wrong_resource' (expecting 'my_resource') in "
            "singleton Delta update");
}

TEST(SingletonSubscriptionAdapterTest, SotWMultipleResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource1").value();

  Protobuf::Arena arena;
  auto message1 = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource1(std::move(message1), "my_resource1", {}, "v1");
  auto message2 = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource2(std::move(message2), "my_resource2", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource1, decoded_resource2};

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  EXPECT_FALSE(adapter->onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaMultipleResources) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource1").value();

  Protobuf::Arena arena;
  auto message1 = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource1(std::move(message1), "my_resource1", {}, "v1");
  auto message2 = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource2(std::move(message2), "my_resource2", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource1, decoded_resource2};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  EXPECT_FALSE(adapter->onConfigUpdate(added_resources, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaEmptyUpdate) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  EXPECT_CALL(callbacks, onResourceRemoved()).Times(0);
  EXPECT_TRUE(adapter->onConfigUpdate({}, {}, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaMultipleRemovals) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource1").value();

  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("my_resource1");
  removed_resources.Add("my_resource2");

  EXPECT_CALL(callbacks, onResourceRemoved()).Times(0);
  EXPECT_FALSE(adapter->onConfigUpdate({}, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, OnConfigUpdateFailed) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  EnvoyException ex("test exception");
  EXPECT_CALL(callbacks, onFailure(ConfigUpdateFailureReason::ConnectionFailure, &ex));
  adapter->onConfigUpdateFailed(ConfigUpdateFailureReason::ConnectionFailure, &ex);
}

TEST(SingletonSubscriptionAdapterTest, OnConfigUpdateInvalidResourceName) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(std::move(message), "xdstp://invalid", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  auto sotw_status = adapter->onConfigUpdate(resources, "v1");
  EXPECT_FALSE(sotw_status.ok());
  EXPECT_EQ(sotw_status.code(), absl::StatusCode::kInvalidArgument);

  // Delta added with invalid resource name
  Protobuf::RepeatedPtrField<std::string> empty_removed;
  auto delta_add_status = adapter->onConfigUpdate(resources, empty_removed, "v1");
  EXPECT_FALSE(delta_add_status.ok());
  EXPECT_EQ(delta_add_status.code(), absl::StatusCode::kInvalidArgument);

  // Delta removed with invalid resource name
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("xdstp://invalid");
  EXPECT_CALL(callbacks, onResourceRemoved()).Times(0);
  auto delta_remove_status = adapter->onConfigUpdate({}, removed_resources, "v1");
  EXPECT_FALSE(delta_remove_status.ok());
  EXPECT_EQ(delta_remove_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SingletonSubscriptionAdapterTest, SotWXdsTpResourceNameNormalization) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(
                     callbacks, "xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1")
                     .value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(
      std::move(message), "xdstp://authority/envoy.type.v3.TokenBucket/bucket?a=1&b=2", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, SotWXdsTpResourceNameNormalizationInverse) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(
                     callbacks, "xdstp://authority/envoy.type.v3.TokenBucket/bucket?a=1&b=2")
                     .value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(
      std::move(message), "xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1", {}, "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaXdsTpResourceNameNormalization) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(
                     callbacks, "xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1")
                     .value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(
      std::move(message), "xdstp://authority/envoy.type.v3.TokenBucket/bucket?a=1&b=2", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(added_resources, removed_resources, "v1").ok());

  // Test removal
  removed_resources.Add("xdstp://authority/envoy.type.v3.TokenBucket/bucket?a=1&b=2");
  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter->onConfigUpdate({}, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, DeltaXdsTpResourceNameNormalizationInverse) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(
                     callbacks, "xdstp://authority/envoy.type.v3.TokenBucket/bucket?a=1&b=2")
                     .value();

  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(
      std::move(message), "xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1", {}, "v1");
  std::vector<DecodedResourceRef> added_resources{decoded_resource};
  Protobuf::RepeatedPtrField<std::string> removed_resources;

  EXPECT_CALL(callbacks, onResourceUpdate(Ref(decoded_resource), "v1"))
      .WillOnce(Return(absl::OkStatus()));
  EXPECT_TRUE(adapter->onConfigUpdate(added_resources, removed_resources, "v1").ok());

  // Test removal
  removed_resources.Add("xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1");
  EXPECT_CALL(callbacks, onResourceRemoved());
  EXPECT_TRUE(adapter->onConfigUpdate({}, removed_resources, "v1").ok());
}

TEST(SingletonSubscriptionAdapterTest, XdsTpResourceNameMismatch) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(
                     callbacks, "xdstp://authority/envoy.type.v3.TokenBucket/bucket?b=2&a=1")
                     .value();

  // SotW mismatch
  Protobuf::Arena arena;
  auto message = Protobuf::Arena::MakeUnique<Protobuf::Empty>(&arena);
  DecodedResourceImpl decoded_resource(
      std::move(message), "xdstp://authority/envoy.type.v3.TokenBucket/other_bucket?a=1&b=2", {},
      "v1");
  std::vector<DecodedResourceRef> resources{decoded_resource};

  EXPECT_CALL(callbacks, onResourceUpdate(_, _)).Times(0);
  auto sotw_status = adapter->onConfigUpdate(resources, "v1");
  EXPECT_FALSE(sotw_status.ok());
  EXPECT_EQ(sotw_status.code(), absl::StatusCode::kInvalidArgument);

  // Delta added mismatch
  Protobuf::RepeatedPtrField<std::string> empty_removed;
  auto delta_add_status = adapter->onConfigUpdate(resources, empty_removed, "v1");
  EXPECT_FALSE(delta_add_status.ok());
  EXPECT_EQ(delta_add_status.code(), absl::StatusCode::kInvalidArgument);

  // Delta removed mismatch
  Protobuf::RepeatedPtrField<std::string> removed_resources;
  removed_resources.Add("xdstp://authority/envoy.type.v3.TokenBucket/other_bucket?a=1&b=2");
  EXPECT_CALL(callbacks, onResourceRemoved()).Times(0);
  auto delta_remove_status = adapter->onConfigUpdate({}, removed_resources, "v1");
  EXPECT_FALSE(delta_remove_status.ok());
  EXPECT_EQ(delta_remove_status.code(), absl::StatusCode::kInvalidArgument);
}

TEST(SingletonSubscriptionImplTest, Start) {
  MockSingletonSubscriptionCallbacks callbacks;
  auto adapter = SingletonSubscriptionCallbacksAdapter::create(callbacks, "my_resource").value();
  auto sub = std::make_unique<NiceMock<MockSubscription>>();
  MockSubscription* raw_sub = sub.get();

  SingletonSubscriptionImpl singleton_sub(std::move(sub), "my_resource", std::move(adapter));

  EXPECT_CALL(*raw_sub, start(absl::flat_hash_set<std::string>{"my_resource"}));
  singleton_sub.start();
}

} // namespace
} // namespace Config
} // namespace Envoy
