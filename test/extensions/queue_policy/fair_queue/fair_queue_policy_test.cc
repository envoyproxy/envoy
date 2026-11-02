#include <chrono>
#include <cmath>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include <vector>

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/extensions/queue_policy/fair_queue/v3/fair_queue.pb.h"
#include "envoy/registry/registry.h"
#include "envoy/type/metadata/v3/metadata.pb.h"

#include "source/common/config/metadata.h"
#include "source/common/conn_pool/pending_stream.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/queue_policy/fair_queue/fair_queue_policy.h"

#include "test/mocks/stream_info/mocks.h"
#include "test/test_common/utility.h"

#include "absl/container/flat_hash_set.h"
#include "gmock/gmock.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {
namespace {

using FairQueuePolicyConfig =
    envoy::extensions::queue_policy::fair_queue::v3::FairQueuePolicyConfig;
using ::testing::NiceMock;

constexpr absl::string_view FilterName = "envoy.filters.http.header_to_metadata";
constexpr absl::string_view KeyName = "tenant_id";

// Lightweight item wrapper used to test FairQueue behavior.
class TestQueueItem {
public:
  explicit TestQueueItem(int id) : id_(id) {}
  int id() const { return id_; }

private:
  int id_;
};

// Builds a MetadataKey proto and wrapper for FilterName / KeyName.
Envoy::Config::MetadataKey makeTenantMetadataKey() {
  envoy::type::metadata::v3::MetadataKey proto_key;
  proto_key.set_key(std::string(FilterName));
  proto_key.add_path()->set_key(std::string(KeyName));
  return Envoy::Config::MetadataKey(proto_key);
}

// Holds a MockStreamInfo with its backing metadata_ proto populated for tests.
class TenantStreamInfo {
public:
  // Creates a StreamInfo with no tenant metadata populated.
  TenantStreamInfo() = default;

  // Creates a StreamInfo with the specified Protobuf::Value stored at FilterName / KeyName.
  explicit TenantStreamInfo(const Protobuf::Value& value) {
    (*(*stream_info_.metadata_.mutable_filter_metadata())[std::string(FilterName)]
          .mutable_fields())[std::string(KeyName)] = value;
  }

  // Creates a StreamInfo with a string tenant identifier.
  explicit TenantStreamInfo(absl::string_view tenant_id)
      : TenantStreamInfo(ValueUtil::stringValue(tenant_id)) {}

  // Returns a mutable reference to the filter metadata struct for custom nested paths.
  Protobuf::Struct& filterStruct() {
    return (*stream_info_.metadata_.mutable_filter_metadata())[std::string(FilterName)];
  }

  // Returns an OptRef to the underlying StreamInfo.
  OptRef<const StreamInfo::StreamInfo> optRef() const {
    return makeOptRef<const StreamInfo::StreamInfo>(stream_info_);
  }

private:
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

// Creates a MonotonicTime from milliseconds since epoch.
MonotonicTime timeFromMs(int64_t ms) { return MonotonicTime(std::chrono::milliseconds(ms)); }

// Drains all items from the queue using peek() and pop(), returning their IDs in dequeue order.
std::vector<int> drainQueueIds(FairQueue<TestQueueItem>& queue) {
  std::vector<int> drained;
  while (!queue.empty()) {
    const int peeked_id = queue.peek().id();
    const int popped_id = queue.pop().id();
    EXPECT_EQ(peeked_id, popped_id);
    drained.push_back(popped_id);
  }
  return drained;
}

TEST(FairQueueTest, BasicQueueOperationsAndRoundRobinFairness) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  EXPECT_FALSE(queue.isOverloaded());

  // Enqueue 3 items for tenant_a, 2 for tenant_b, and 1 for tenant_c.
  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TenantStreamInfo tenant_c("tenant_c");

  TestQueueItem a1(1), a2(2), a3(3);
  TestQueueItem b1(10), b2(20);
  TestQueueItem c1(100);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});

  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 6);

  static_assert(std::is_same_v<decltype(queue.peek()), TestQueueItem&>);
  static_assert(std::is_same_v<decltype(queue.pop()), TestQueueItem&>);
  const FairQueue<TestQueueItem>& const_queue = queue;
  static_assert(std::is_same_v<decltype(const_queue.peek()), const TestQueueItem&>);
  EXPECT_EQ(const_queue.peek().id(), 1);

  // Dequeue order must interleave tenants in round-robin rounds:
  // Round 1: a1 (1), b1 (10), c1 (100).
  // Round 2: a2 (2), b2 (20).
  // Round 3: a3 (3).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 10, 100, 2, 20, 3}));
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
}

TEST(FairQueueTest, SingleBucketAndZeroBucketClampDegradeToStrictFifo) {
  for (uint32_t configured_buckets : {0u, 1u}) {
    FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), configured_buckets);
    TenantStreamInfo tenant_a("tenant_a");
    TenantStreamInfo tenant_b("tenant_b");

    TestQueueItem a1(1), a2(2), b1(3), untagged(4);
    queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
    queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
    queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
    queue.add(untagged, {timeFromMs(0), {}});

    // With a single bucket, all classes share bucket 0 and drain in arrival order.
    EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 3, 4}));
  }
}

TEST(FairQueueTest, MissingAndNullMetadataShareSingleDefaultClass) {
  // Use 2 buckets and pick a tagged tenant that hashes to the bucket opposite the default class.
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2);

  TenantStreamInfo missing_key_info;
  TenantStreamInfo null_value_info(ValueUtil::nullValue());
  // Find a tenant string that lands in the other bucket from the default class at seed 0.
  const uint64_t default_bucket = HashUtil::xxHash64Value(uint32_t{0}, /*seed=*/0) % 2;
  std::string tagged_name = "tenant_0";
  for (int i = 0; i < 100; ++i) {
    std::string candidate = absl::StrCat("tenant_", i);
    const uint64_t type_seed =
        HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
    if ((HashUtil::xxHash64(candidate, type_seed) % 2) != default_bucket) {
      tagged_name = candidate;
      break;
    }
  }
  TenantStreamInfo tagged_info(tagged_name);

  TestQueueItem untagged_no_stream_info(1);
  TestQueueItem untagged_missing_key(2);
  TestQueueItem untagged_null_value(3);
  TestQueueItem tagged_item(99);

  // Enqueue all three untagged variants first, then the tagged item.
  queue.add(untagged_no_stream_info, {timeFromMs(0), {}});
  queue.add(untagged_missing_key, {timeFromMs(0), missing_key_info.optRef()});
  queue.add(untagged_null_value, {timeFromMs(0), null_value_info.optRef()});
  queue.add(tagged_item, {timeFromMs(0), tagged_info.optRef()});

  // Because all three untagged items share one default bucket, the tagged item is served second
  // rather than fourth.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 99, 2, 3}));
}

TEST(FairQueueTest, SupportsAllProtobufValueKinds) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  Protobuf::Struct struct_val;
  (*struct_val.mutable_fields())["nested"] = ValueUtil::stringValue("v");

  TenantStreamInfo number_info(ValueUtil::numberValue(42.0));
  TenantStreamInfo bool_info(ValueUtil::boolValue(true));
  TenantStreamInfo struct_info(ValueUtil::structValue(struct_val));
  TenantStreamInfo list_info(
      ValueUtil::listValue({ValueUtil::stringValue("a"), ValueUtil::numberValue(1)}));

  TestQueueItem i1(1), i2(2), i3(3), i4(4);
  queue.add(i1, {timeFromMs(0), number_info.optRef()});
  queue.add(i2, {timeFromMs(0), bool_info.optRef()});
  queue.add(i3, {timeFromMs(0), struct_info.optRef()});
  queue.add(i4, {timeFromMs(0), list_info.optRef()});

  EXPECT_EQ(queue.size(), 4);
  const std::vector<int> drained = drainQueueIds(queue);
  EXPECT_EQ(drained.size(), 4);
}

TEST(FairQueueTest, NestedMetadataKeyPathAndSpecialScalarValues) {
  envoy::type::metadata::v3::MetadataKey nested_proto_key;
  nested_proto_key.set_key(std::string(FilterName));
  nested_proto_key.add_path()->set_key("auth");
  nested_proto_key.add_path()->set_key("tenant_id");
  FairQueue<TestQueueItem> queue(Envoy::Config::MetadataKey(nested_proto_key),
                                 /*num_buckets=*/1024);

  auto setNestedStringTenant = [](TenantStreamInfo& info, absl::string_view tenant) {
    Protobuf::Struct auth_struct;
    (*auth_struct.mutable_fields())["tenant_id"] = ValueUtil::stringValue(tenant);
    (*info.filterStruct().mutable_fields())["auth"] = ValueUtil::structValue(auth_struct);
  };

  TenantStreamInfo nested_a;
  setNestedStringTenant(nested_a, "nested_tenant_a");
  TenantStreamInfo nested_b;
  setNestedStringTenant(nested_b, "nested_tenant_b");
  // Partial path ("auth" is a string instead of a struct) falls back to the default class.
  TenantStreamInfo partial_path_info;
  (*partial_path_info.filterStruct().mutable_fields())["auth"] =
      ValueUtil::stringValue("not_a_struct");

  TestQueueItem a1(1), a2(2), b1(10), partial(99);
  queue.add(a1, {timeFromMs(0), nested_a.optRef()});
  queue.add(a2, {timeFromMs(0), nested_a.optRef()});
  queue.add(b1, {timeFromMs(0), nested_b.optRef()});
  queue.add(partial, {timeFromMs(0), partial_path_info.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 10, 99, 2}));

  // Verify special floating-point metadata values (0.0, -0.0, NaN, +Inf, -Inf), false, and "".
  FairQueue<TestQueueItem> special_queue(makeTenantMetadataKey(), /*num_buckets=*/1024);
  TenantStreamInfo zero_num(ValueUtil::numberValue(0.0));
  TenantStreamInfo neg_zero_num(ValueUtil::numberValue(-0.0));
  TenantStreamInfo nan_num(ValueUtil::numberValue(std::numeric_limits<double>::quiet_NaN()));
  TenantStreamInfo pos_inf_num(ValueUtil::numberValue(std::numeric_limits<double>::infinity()));
  TenantStreamInfo neg_inf_num(ValueUtil::numberValue(-std::numeric_limits<double>::infinity()));
  TenantStreamInfo false_bool(ValueUtil::boolValue(false));
  TenantStreamInfo empty_str(ValueUtil::stringValue(""));

  TestQueueItem s1(1), s2(2), s3(3), s4(4), s5(5), s6(6), s7(7);
  special_queue.add(s1, {timeFromMs(0), zero_num.optRef()});
  special_queue.add(s2, {timeFromMs(0), neg_zero_num.optRef()});
  special_queue.add(s3, {timeFromMs(0), nan_num.optRef()});
  special_queue.add(s4, {timeFromMs(0), pos_inf_num.optRef()});
  special_queue.add(s5, {timeFromMs(0), neg_inf_num.optRef()});
  special_queue.add(s6, {timeFromMs(0), false_bool.optRef()});
  special_queue.add(s7, {timeFromMs(0), empty_str.optRef()});
  EXPECT_EQ(special_queue.size(), 7);
  EXPECT_EQ(drainQueueIds(special_queue).size(), 7);
}

TEST(FairQueueTest, RemoveHeadMiddleTailAndLastActiveItem) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TenantStreamInfo tenant_c("tenant_c");

  TestQueueItem a1(1), a2(2), a3(3), a4(4);
  TestQueueItem b1(10), b2(20);
  TestQueueItem c1(100);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a4, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});

  // Remove middle (a2) and tail (a4) from tenant_a's bucket; a1 remains at the front.
  queue.remove(a2);
  queue.remove(a4);
  EXPECT_EQ(queue.size(), 5);
  EXPECT_EQ(queue.peek().id(), 1);

  // Remove the sole item (c1) in tenant_c's bucket, deactivating tenant_c.
  queue.remove(c1);
  EXPECT_EQ(queue.size(), 4);

  // Remove the head (a1) and remaining item (a3) of tenant_a; tenant_b (b1) becomes the new head.
  queue.remove(a1);
  EXPECT_EQ(queue.peek().id(), 3);
  queue.remove(a3);
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.peek().id(), 10);

  // Drain the remaining items in tenant_b's bucket.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{10, 20}));
  EXPECT_TRUE(queue.empty());
}

TEST(FairQueueTest, BucketDeactivationAndReactivationJoinBackOfRoundRobin) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TenantStreamInfo tenant_c("tenant_c");

  TestQueueItem a1(1), a2(2), a3(3);
  TestQueueItem b1(10), b2(20), b3(30);
  TestQueueItem c1(100);

  // Initial active bucket order: [A, B].
  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});

  // Pop a1: bucket A still has [a2, a3], so active order rotates to [B, A].
  EXPECT_EQ(queue.pop().id(), 1);
  // Pop b1: bucket B becomes empty and is deactivated; active order is now [A].
  EXPECT_EQ(queue.pop().id(), 10);

  // Enqueue c1: bucket C activates at the back; active order is [A, C].
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});
  // Reactivate bucket B with b2: bucket B must join at the back; active order is [A, C, B].
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});

  // Deactivate bucket B via remove(b2) and reactivate again with b3: active order remains [A, C,
  // B].
  queue.remove(b2);
  queue.add(b3, {timeFromMs(0), tenant_b.optRef()});

  // Expected dequeue order from [A([a2, a3]), C([c1]), B([b3])]: a2 (2), c1 (100), b3 (30), a3 (3).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{2, 100, 30, 3}));
}

TEST(FairQueueTest, ForEachRespectsRotatedRoundRobinTurnAndFutureItemRemoval) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);

  // Empty queue forEach is a no-op.
  int empty_visits = 0;
  queue.forEach([&empty_visits](TestQueueItem&) -> bool {
    ++empty_visits;
    return true;
  });
  EXPECT_EQ(empty_visits, 0);

  TenantStreamInfo tenant_a("tenant_a");
  TenantStreamInfo tenant_b("tenant_b");
  TenantStreamInfo tenant_c("tenant_c");
  TestQueueItem a1(1), a2(2), a3(3);
  TestQueueItem b1(10), b2(20);
  TestQueueItem c1(100);

  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});

  // Pop a1 so the active bucket turn rotates from [A, B, C] to [B, C, A].
  EXPECT_EQ(queue.pop().id(), 1);

  // Full traversal from rotated state visits in exact dequeue order: b1, c1, a2, b2, a3.
  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{10, 100, 2, 20, 3}));

  // Returning false stops iteration early.
  visited.clear();
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return item.id() != 100;
  });
  EXPECT_EQ(visited, (std::vector<int>{10, 100}));

  // Removing both the currently visited item (b1) and a future unvisited item (b2 and c1) inside
  // the callback skips the removed future items safely without visiting them.
  visited.clear();
  queue.forEach([&queue, &visited, &c1, &b2](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    if (item.id() == 10) {
      queue.remove(item);
      queue.remove(c1);
      queue.remove(b2);
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{10, 2, 3}));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{2, 3}));
}

TEST(FairQueueTest, PerturbationPreservesQueuedBucketForRemoveAndHandlesMultiEpochJumps) {
  // Find two tenants that collide at seed 0 in a 2-bucket table, separate at seed 1, and where
  // colliding_b changes bucket between seed 0 and seed 2.
  const uint64_t seed0_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
  const uint64_t seed1_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/1);
  const uint64_t seed2_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/2);

  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;

  for (int i = 0; i < 400; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const uint64_t b0 = HashUtil::xxHash64(candidate, seed0_type) % 2;
    const uint64_t b1 = HashUtil::xxHash64(candidate, seed1_type) % 2;
    const uint64_t b2 = HashUtil::xxHash64(candidate, seed2_type) % 2;
    if (colliding_a.empty() && b0 == 0 && b1 == 0 && b2 == 0) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b0 == 0 && b1 == 1 && b2 == 1) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b0 == 1) {
      other_seed0 = candidate;
    }
  }
  ASSERT_FALSE(colliding_a.empty());
  ASSERT_FALSE(colliding_b.empty());
  ASSERT_FALSE(other_seed0.empty());

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2,
                                 std::chrono::milliseconds(1000));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  // At t = 0ms..999ms (seed 0), colliding_a and colliding_b both land in bucket 0.
  TestQueueItem item_a(1), item_b_early(2), item_other(3);
  queue.add(item_a, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b_early, {timeFromMs(999), info_b.optRef()});
  queue.add(item_other, {timeFromMs(999), info_other.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 3, 2}));

  // Enqueue item_b_pre_perturb at t = 999ms (seed 0, bucket 0) and keep it in the queue while
  // subsequent arrivals advance the seed at t = 1000ms (seed 1) and t = 2500ms (seed 2).
  TestQueueItem item_b_pre_perturb(10), item_a_seed1(11), item_b_seed1(12), item_b_seed2(13);
  queue.add(item_b_pre_perturb, {timeFromMs(999), info_b.optRef()});
  queue.add(item_a_seed1, {timeFromMs(1000), info_a.optRef()}); // seed 1 -> bucket 0
  // Non-monotonic timestamp (t = 950ms) and sub-interval timestamp (t = 1999ms) stay at seed 1.
  queue.add(item_b_seed1, {timeFromMs(950), info_b.optRef()});  // seed 1 -> bucket 1
  queue.add(item_b_seed2, {timeFromMs(2500), info_b.optRef()}); // seed 2 -> bucket 1

  // Removing item_b_pre_perturb must remove it from bucket 0 (where it was placed at seed 0)
  // even though info_b now hashes to bucket 1 under seed 2.
  queue.remove(item_b_pre_perturb);
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{11, 12, 13}));
}

TEST(FairQueueTest, ZeroPerturbationIntervalDisablesSeedRotation) {
  const uint64_t seed0_type =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;
  for (int i = 0; i < 200; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const uint64_t b0 = HashUtil::xxHash64(candidate, seed0_type) % 2;
    if (colliding_a.empty() && b0 == 0) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b0 == 0) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b0 == 1) {
      other_seed0 = candidate;
    }
  }

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/2,
                                 std::chrono::milliseconds(0));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  TestQueueItem item_a(1), item_b(2), item_other(3);
  queue.add(item_a, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b, {timeFromMs(5000), info_b.optRef()});
  queue.add(item_other, {timeFromMs(5000), info_other.optRef()});
  // Even after 5000ms, seed remains 0 so colliding_a and colliding_b stay in bucket 0.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 3, 2}));
}

TEST(FairQueueTest, NoisyNeighborStarvationProtectionHighCardinality) {
  constexpr uint32_t num_buckets = 1024;
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets);

  const uint64_t type_seed =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);
  const std::string noisy_name = "noisy_tenant";
  const uint32_t noisy_bucket =
      static_cast<uint32_t>(HashUtil::xxHash64(noisy_name, type_seed) % num_buckets);

  // Select 10 well-behaved tenants that hash to distinct buckets different from noisy_bucket.
  absl::flat_hash_set<uint32_t> used_buckets = {noisy_bucket};
  std::vector<std::unique_ptr<TenantStreamInfo>> wb_tenants;
  for (int i = 0; wb_tenants.size() < 10; ++i) {
    const std::string candidate = absl::StrCat("wb_tenant_", i);
    const uint32_t bucket =
        static_cast<uint32_t>(HashUtil::xxHash64(candidate, type_seed) % num_buckets);
    if (used_buckets.insert(bucket).second) {
      wb_tenants.push_back(std::make_unique<TenantStreamInfo>(candidate));
    }
  }

  // Enqueue 100 items for the noisy tenant first.
  TenantStreamInfo noisy_info(noisy_name);
  std::vector<TestQueueItem> noisy_items;
  noisy_items.reserve(100);
  for (int i = 0; i < 100; ++i) {
    noisy_items.emplace_back(1000 + i);
    queue.add(noisy_items.back(), {timeFromMs(0), noisy_info.optRef()});
  }

  // Enqueue 1 item for each of the 10 well-behaved tenants after the 100-item burst.
  std::vector<TestQueueItem> wb_items;
  wb_items.reserve(wb_tenants.size());
  for (size_t i = 0; i < wb_tenants.size(); ++i) {
    wb_items.emplace_back(static_cast<int>(i + 1));
    queue.add(wb_items.back(), {timeFromMs(0), wb_tenants[i]->optRef()});
  }

  // The first pop serves noisy_items[0] (1000), and the next 10 pops immediately serve all 10
  // well-behaved tenants (1..10) ahead of noisy_items[1..99].
  EXPECT_EQ(queue.pop().id(), 1000);
  for (int expected_wb_id = 1; expected_wb_id <= 10; ++expected_wb_id) {
    EXPECT_EQ(queue.pop().id(), expected_wb_id);
  }
  EXPECT_EQ(queue.size(), 99);
  EXPECT_EQ(queue.peek().id(), 1001);
}

class FairQueueFactoryTest : public ::testing::Test {
protected:
  FairQueueFactory<TestQueueItem> factory_;
};

TEST_F(FairQueueFactoryTest, FactoryMetadataAndEmptyProto) {
  EXPECT_EQ(factory_.name(), "envoy.queue_policy.fair_queue");
  EXPECT_EQ(factory_.category(), "envoy.queue_policy");
  EXPECT_NE(factory_.createEmptyConfigProto(), nullptr);
}

TEST_F(FairQueueFactoryTest, CreateQueuePolicyWithDefaultsAndCustomConfig) {
  FairQueuePolicyConfig config;
  config.mutable_class_metadata_key()->set_key(std::string(FilterName));
  config.mutable_class_metadata_key()->add_path()->set_key(std::string(KeyName));

  auto default_result = factory_.createQueuePolicy(config, "test_prefix",
                                                   ProtobufMessage::getStrictValidationVisitor());
  EXPECT_TRUE(default_result.ok());
  EXPECT_NE(default_result.value(), nullptr);

  config.mutable_num_buckets()->set_value(256);
  config.mutable_perturbation_interval()->set_seconds(2);
  auto custom_result = factory_.createQueuePolicy(config, "test_prefix",
                                                  ProtobufMessage::getStrictValidationVisitor());
  EXPECT_TRUE(custom_result.ok());
  EXPECT_NE(custom_result.value(), nullptr);
}

TEST_F(FairQueueFactoryTest, ValidationRejectsMissingMetadataKeyOrZeroBuckets) {
  FairQueuePolicyConfig empty_config;
  EXPECT_THROW(factory_
                   .createQueuePolicy(empty_config, "test_prefix",
                                      ProtobufMessage::getStrictValidationVisitor())
                   .IgnoreError(),
               ProtoValidationException);

  FairQueuePolicyConfig zero_buckets_config;
  zero_buckets_config.mutable_class_metadata_key()->set_key(std::string(FilterName));
  zero_buckets_config.mutable_class_metadata_key()->add_path()->set_key(std::string(KeyName));
  zero_buckets_config.mutable_num_buckets()->set_value(0);
  EXPECT_THROW(factory_
                   .createQueuePolicy(zero_buckets_config, "test_prefix",
                                      ProtobufMessage::getStrictValidationVisitor())
                   .IgnoreError(),
               ProtoValidationException);
}

TEST_F(FairQueueFactoryTest, RegisteredPendingStreamFactoryCreatesWorkingQueue) {
  auto* registered_factory =
      Registry::FactoryRegistry<QueuePolicyFactory<ConnectionPool::PendingStream>>::getFactory(
          "envoy.queue_policy.fair_queue");
  ASSERT_NE(registered_factory, nullptr);

  FairQueuePolicyConfig config;
  config.mutable_class_metadata_key()->set_key(std::string(FilterName));
  config.mutable_class_metadata_key()->add_path()->set_key(std::string(KeyName));
  auto queue_or = registered_factory->createQueuePolicy(
      config, "cluster.test", ProtobufMessage::getStrictValidationVisitor());
  ASSERT_TRUE(queue_or.ok());
  EXPECT_NE(queue_or.value(), nullptr);
  EXPECT_TRUE((*queue_or)->empty());
}

} // namespace
} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
