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

// Computes the two candidate bucket indices for a string tenant at a given perturbation seed.
std::pair<uint32_t, uint32_t> computeTwoChoices(absl::string_view tenant, uint64_t seed,
                                                uint32_t num_buckets) {
  const uint64_t type_seed =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), seed);
  const uint64_t hash = HashUtil::xxHash64(tenant, type_seed);
  const uint32_t b1 = static_cast<uint32_t>(hash % num_buckets);
  uint32_t b2 = static_cast<uint32_t>((hash >> 32) % num_buckets);
  if (b2 == b1 && num_buckets > 1) {
    b2 = (b1 + 1 < num_buckets) ? (b1 + 1) : 0;
  }
  return {b1, b2};
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

  // Two-choice selection places a1 in A's 1st bucket, a2 in A's 2nd bucket, and a3 in A's 1st
  // bucket; b1 and b2 in B's 1st and 2nd buckets; and c1 in C's 1st bucket.
  // Round 1 drains one item from each active bucket: a1 (1), a2 (2), b1 (10), b2 (20), c1 (100).
  // Round 2 drains the remaining item from A's 1st bucket: a3 (3).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 10, 20, 100, 3}));
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

  TestQueueItem a1(1), a2(2), a3(3), b1(10), partial(99);
  queue.add(a1, {timeFromMs(0), nested_a.optRef()});
  queue.add(a2, {timeFromMs(0), nested_a.optRef()});
  queue.add(a3, {timeFromMs(0), nested_a.optRef()});
  queue.add(b1, {timeFromMs(0), nested_b.optRef()});
  queue.add(partial, {timeFromMs(0), partial_path_info.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 10, 99, 3}));

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

  // Enqueue 4 untagged items into the single default bucket, 2 items for tenant_b, and 1 for
  // tenant_c.
  TestQueueItem u1(1), u2(2), u3(3), u4(4);
  TestQueueItem b1(10), b2(20);
  TestQueueItem c1(100);

  queue.add(u1, {timeFromMs(0), {}});
  queue.add(u2, {timeFromMs(0), {}});
  queue.add(u3, {timeFromMs(0), {}});
  queue.add(u4, {timeFromMs(0), {}});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});

  // Remove middle (u2) and tail (u4) from the default bucket; u1 remains at the front.
  queue.remove(u2);
  queue.remove(u4);
  EXPECT_EQ(queue.size(), 5);
  EXPECT_EQ(queue.peek().id(), 1);

  // Remove the sole item (c1) in tenant_c's bucket, deactivating tenant_c.
  queue.remove(c1);
  EXPECT_EQ(queue.size(), 4);

  // Remove the head (u1) and remaining item (u3) of the default bucket; tenant_b (b1) becomes the
  // new head.
  queue.remove(u1);
  EXPECT_EQ(queue.peek().id(), 3);
  queue.remove(u3);
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(queue.peek().id(), 10);

  // Drain the remaining items in tenant_b's buckets.
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

  // Initial active bucket order: [A1([a1, a3]), A2([a2]), B1([b1])].
  queue.add(a1, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a2, {timeFromMs(0), tenant_a.optRef()});
  queue.add(a3, {timeFromMs(0), tenant_a.optRef()});
  queue.add(b1, {timeFromMs(0), tenant_b.optRef()});

  // Pop a1: bucket A1 still has [a3], so active order rotates to [A2, B1, A1].
  EXPECT_EQ(queue.pop().id(), 1);
  // Pop a2 and b1: buckets A2 and B1 become empty and are deactivated; active order is now [A1].
  EXPECT_EQ(queue.pop().id(), 2);
  EXPECT_EQ(queue.pop().id(), 10);

  // Enqueue c1: bucket C1 activates at the back; active order is [A1, C1].
  queue.add(c1, {timeFromMs(0), tenant_c.optRef()});
  // Reactivate bucket B1 with b2: bucket B1 must join at the back; active order is [A1, C1, B1].
  queue.add(b2, {timeFromMs(0), tenant_b.optRef()});

  // Deactivate bucket B1 via remove(b2) and reactivate again with b3: active order remains
  // [A1, C1, B1].
  queue.remove(b2);
  queue.add(b3, {timeFromMs(0), tenant_b.optRef()});

  // Expected dequeue order from [A1([a3]), C1([c1]), B1([b3])]: a3 (3), c1 (100), b3 (30).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{3, 100, 30}));
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

  // Pop a1 so the active bucket turn rotates from [A1([a1, a3]), A2([a2]), B1([b1]), B2([b2]),
  // C1([c1])] to [A2([a2]), B1([b1]), B2([b2]), C1([c1]), A1([a3])].
  EXPECT_EQ(queue.pop().id(), 1);

  // Full traversal from rotated state visits in exact dequeue order: a2, b1, b2, c1, a3.
  std::vector<int> visited;
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{2, 10, 20, 100, 3}));

  // Returning false stops iteration early.
  visited.clear();
  queue.forEach([&visited](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    return item.id() != 10;
  });
  EXPECT_EQ(visited, (std::vector<int>{2, 10}));

  // Removing both the currently visited item (a2) and future unvisited items (b2 and c1) inside
  // the callback skips the removed future items safely without visiting them.
  visited.clear();
  queue.forEach([&queue, &visited, &c1, &b2](TestQueueItem& item) -> bool {
    visited.push_back(item.id());
    if (item.id() == 2) {
      queue.remove(item);
      queue.remove(c1);
      queue.remove(b2);
    }
    return true;
  });
  EXPECT_EQ(visited, (std::vector<int>{2, 10, 3}));
  EXPECT_EQ(queue.size(), 2);
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{10, 3}));
}

TEST(FairQueueTest, PerturbationPreservesQueuedBucketForRemoveAndHandlesMultiEpochJumps) {
  constexpr uint32_t num_buckets = 4;
  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;

  for (int i = 0; i < 2000; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const auto [b1_s0, b2_s0] = computeTwoChoices(candidate, /*seed=*/0, num_buckets);
    const auto [b1_s1, b2_s1] = computeTwoChoices(candidate, /*seed=*/1, num_buckets);
    const auto [b1_s2, b2_s2] = computeTwoChoices(candidate, /*seed=*/2, num_buckets);
    if (colliding_a.empty() && b1_s0 == 0 && b2_s0 == 1 && b1_s1 == 0 && b1_s2 == 0) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b1_s0 == 0 && b2_s0 == 1 && b1_s1 == 2 && b1_s2 == 3) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b1_s0 == 2) {
      other_seed0 = candidate;
    }
  }
  ASSERT_FALSE(colliding_a.empty());
  ASSERT_FALSE(colliding_b.empty());
  ASSERT_FALSE(other_seed0.empty());

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets,
                                 std::chrono::milliseconds(1000));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  // At t = 0ms..999ms (seed 0), colliding_a and colliding_b share candidate pair (0, 1).
  // Enqueue 2 items for colliding_a (filling buckets 0 and 1 to depth 1), then item_b_early (which
  // lands in bucket 0 behind item_a1), then item_other (landing in bucket 2).
  TestQueueItem item_a1(1), item_a2(2), item_b_early(3), item_other(4);
  queue.add(item_a1, {timeFromMs(0), info_a.optRef()});
  queue.add(item_a2, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b_early, {timeFromMs(999), info_b.optRef()});
  queue.add(item_other, {timeFromMs(999), info_other.optRef()});
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 4, 3}));

  // Enqueue item_b_pre_perturb at t = 999ms (seed 0, bucket 0) and keep it in the queue while
  // subsequent arrivals advance the seed at t = 1000ms (seed 1) and t = 2500ms (seed 2).
  TestQueueItem item_b_pre_perturb(10), item_a_seed1(11), item_b_seed1(12), item_b_seed2(13);
  queue.add(item_b_pre_perturb, {timeFromMs(999), info_b.optRef()});
  queue.add(item_a_seed1, {timeFromMs(1000), info_a.optRef()});
  // Non-monotonic timestamp (t = 950ms) and sub-interval timestamp (t = 1999ms) stay at seed 1.
  queue.add(item_b_seed1, {timeFromMs(950), info_b.optRef()});  // seed 1 -> bucket 2
  queue.add(item_b_seed2, {timeFromMs(2500), info_b.optRef()}); // seed 2 -> bucket 3

  // Removing item_b_pre_perturb must remove it from bucket 0 (where it was placed at seed 0)
  // even though info_b now hashes to bucket 3 under seed 2.
  queue.remove(item_b_pre_perturb);
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{11, 12, 13}));
}

TEST(FairQueueTest, ZeroPerturbationIntervalDisablesSeedRotation) {
  constexpr uint32_t num_buckets = 4;
  std::string colliding_a;
  std::string colliding_b;
  std::string other_seed0;
  for (int i = 0; i < 400; ++i) {
    const std::string candidate = absl::StrCat("flow_", i);
    const auto [b1, b2] = computeTwoChoices(candidate, /*seed=*/0, num_buckets);
    if (colliding_a.empty() && b1 == 0 && b2 == 1) {
      colliding_a = candidate;
    } else if (colliding_b.empty() && b1 == 0 && b2 == 1) {
      colliding_b = candidate;
    } else if (other_seed0.empty() && b1 == 2) {
      other_seed0 = candidate;
    }
  }
  ASSERT_FALSE(colliding_a.empty());
  ASSERT_FALSE(colliding_b.empty());
  ASSERT_FALSE(other_seed0.empty());

  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets,
                                 std::chrono::milliseconds(0));

  TenantStreamInfo info_a(colliding_a);
  TenantStreamInfo info_b(colliding_b);
  TenantStreamInfo info_other(other_seed0);

  TestQueueItem item_a1(1), item_a2(2), item_b(3), item_other(4);
  queue.add(item_a1, {timeFromMs(0), info_a.optRef()});
  queue.add(item_a2, {timeFromMs(0), info_a.optRef()});
  queue.add(item_b, {timeFromMs(5000), info_b.optRef()});
  queue.add(item_other, {timeFromMs(5000), info_other.optRef()});
  // Even after 5000ms, seed remains 0 so colliding_b still hashes to (0, 1) behind item_a1.
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 4, 3}));
}

TEST(FairQueueTest, LazyAllocationEmptyQueueOperations) {
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), /*num_buckets=*/1024);
  EXPECT_TRUE(queue.empty());
  EXPECT_EQ(queue.size(), 0);
  EXPECT_FALSE(queue.isOverloaded());

  // Traversal on an unallocated, empty queue is a safe no-op.
  int visits = 0;
  queue.forEach([&visits](TestQueueItem&) -> bool {
    ++visits;
    return true;
  });
  EXPECT_EQ(visits, 0);

  // Enqueueing the first item lazily initializes bucket storage.
  TenantStreamInfo tenant("tenant");
  TestQueueItem item(42);
  queue.add(item, {timeFromMs(0), tenant.optRef()});
  EXPECT_FALSE(queue.empty());
  EXPECT_EQ(queue.size(), 1);
  EXPECT_EQ(queue.pop().id(), 42);
  EXPECT_TRUE(queue.empty());
}

TEST(FairQueueTest, TwoChoiceTieBreakerIncrementsCandidateBucket) {
  constexpr uint32_t num_buckets = 6;
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets,
                                 std::chrono::milliseconds(0));

  const uint64_t type_seed =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);

  std::string tiebreaker_tenant;
  uint32_t expected_b1 = 0;
  for (int i = 0; i < 2000; ++i) {
    const std::string candidate = absl::StrCat("candidate_", i);
    const uint64_t hash = HashUtil::xxHash64(candidate, type_seed);
    const uint32_t raw_b1 = static_cast<uint32_t>(hash % num_buckets);
    const uint32_t raw_b2 = static_cast<uint32_t>((hash >> 32) % num_buckets);
    if (raw_b1 == raw_b2 && raw_b1 + 1 < num_buckets) {
      tiebreaker_tenant = candidate;
      expected_b1 = raw_b1;
      break;
    }
  }
  ASSERT_FALSE(tiebreaker_tenant.empty());

  const auto [b1, b2] = computeTwoChoices(tiebreaker_tenant, /*seed=*/0, num_buckets);
  EXPECT_EQ(b1, expected_b1);
  EXPECT_EQ(b2, expected_b1 + 1);

  TenantStreamInfo tenant_info(tiebreaker_tenant);
  TestQueueItem item1(1), item2(2), item3(3);
  // item1 lands in b1 (empty), item2 lands in b2 (empty), item3 lands in b1 (depth 1).
  queue.add(item1, {timeFromMs(0), tenant_info.optRef()});
  queue.add(item2, {timeFromMs(0), tenant_info.optRef()});
  queue.add(item3, {timeFromMs(0), tenant_info.optRef()});

  // Drain order alternates between b1 and b2: item1 (1), item2 (2), item3 (3).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{1, 2, 3}));
}

TEST(FairQueueTest, TwoChoiceBucketSelectionAvoidsPrimaryBucketCollisionAndHandlesNonPowerOfTwo) {
  // Use a non-power-of-two bucket count (6) to also cover the modulo path in hashToBucket().
  constexpr uint32_t num_buckets = 6;
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets,
                                 std::chrono::milliseconds(0));

  const uint64_t type_seed =
      HashUtil::xxHash64Value(static_cast<uint32_t>(Protobuf::Value::kStringValue), /*seed=*/0);

  // Find a noisy tenant with choices (0, 1), a colliding tenant with primary bucket 0 but
  // secondary bucket 2, and a tenant whose raw upper/lower hashes both map to the last bucket (5)
  // to exercise the wrap-around fallback (bucket2 == bucket1 -> 0).
  std::string noisy_tenant;
  std::string colliding_primary_tenant;
  std::string wraparound_tenant;
  std::string increment_tiebreaker_tenant;
  for (int i = 0; i < 2000; ++i) {
    const std::string candidate = absl::StrCat("candidate_", i);
    const uint64_t hash = HashUtil::xxHash64(candidate, type_seed);
    const uint32_t raw_b1 = static_cast<uint32_t>(hash % num_buckets);
    const uint32_t raw_b2 = static_cast<uint32_t>((hash >> 32) % num_buckets);
    const auto [b1, b2] = computeTwoChoices(candidate, /*seed=*/0, num_buckets);
    if (noisy_tenant.empty() && b1 == 0 && b2 == 1) {
      noisy_tenant = candidate;
    } else if (colliding_primary_tenant.empty() && b1 == 0 && b2 == 2) {
      colliding_primary_tenant = candidate;
    } else if (wraparound_tenant.empty() && raw_b1 == num_buckets - 1 &&
               raw_b2 == num_buckets - 1) {
      wraparound_tenant = candidate;
    } else if (increment_tiebreaker_tenant.empty() && raw_b1 == raw_b2 &&
               raw_b1 < num_buckets - 1) {
      increment_tiebreaker_tenant = candidate;
    }
  }
  ASSERT_FALSE(noisy_tenant.empty());
  ASSERT_FALSE(colliding_primary_tenant.empty());
  ASSERT_FALSE(wraparound_tenant.empty());
  ASSERT_FALSE(increment_tiebreaker_tenant.empty());

  TenantStreamInfo noisy_info(noisy_tenant);
  TenantStreamInfo colliding_info(colliding_primary_tenant);
  TenantStreamInfo wraparound_info(wraparound_tenant);

  // Enqueue 4 items for noisy_tenant, filling buckets 0 and 1 to depth 2 each.
  TestQueueItem n1(10), n2(11), n3(12), n4(13);
  queue.add(n1, {timeFromMs(0), noisy_info.optRef()});
  queue.add(n2, {timeFromMs(0), noisy_info.optRef()});
  queue.add(n3, {timeFromMs(0), noisy_info.optRef()});
  queue.add(n4, {timeFromMs(0), noisy_info.optRef()});

  // Enqueue 1 item for colliding_primary_tenant: its primary bucket (0) has depth 2, so two-choice
  // selection places it into its empty secondary bucket (2), serving it in the first round-robin
  // round ahead of n3 and n4.
  TestQueueItem c1(99);
  queue.add(c1, {timeFromMs(0), colliding_info.optRef()});

  // Enqueue 2 items for wraparound_tenant: 1st goes to bucket 5, 2nd wraps around to bucket 0.
  TestQueueItem w1(200), w2(201);
  queue.add(w1, {timeFromMs(0), wraparound_info.optRef()});
  queue.add(w2, {timeFromMs(0), wraparound_info.optRef()});

  // Active buckets are [0([n1, n3, w2]), 1([n2, n4]), 2([c1]), 5([w1])].
  // Round 1 drains: n1 (10), n2 (11), c1 (99), w1 (200).
  // Round 2 drains: n3 (12), n4 (13).
  // Round 3 drains: w2 (201).
  EXPECT_EQ(drainQueueIds(queue), (std::vector<int>{10, 11, 99, 200, 12, 13, 201}));
}

TEST(FairQueueTest, NoisyNeighborStarvationProtectionHighCardinality) {
  constexpr uint32_t num_buckets = 1024;
  FairQueue<TestQueueItem> queue(makeTenantMetadataKey(), num_buckets);

  const std::string noisy_name = "noisy_tenant";
  const auto [noisy_b1, noisy_b2] = computeTwoChoices(noisy_name, /*seed=*/0, num_buckets);

  // Select 10 well-behaved tenants that hash to distinct primary buckets different from noisy_b1
  // and noisy_b2.
  absl::flat_hash_set<uint32_t> used_buckets = {noisy_b1, noisy_b2};
  std::vector<std::unique_ptr<TenantStreamInfo>> wb_tenants;
  for (int i = 0; wb_tenants.size() < 10; ++i) {
    const std::string candidate = absl::StrCat("wb_tenant_", i);
    const auto [b1, b2] = computeTwoChoices(candidate, /*seed=*/0, num_buckets);
    if (used_buckets.insert(b1).second) {
      wb_tenants.push_back(std::make_unique<TenantStreamInfo>(candidate));
    }
  }

  // Enqueue 100 items for the noisy tenant first (50 in noisy_b1, 50 in noisy_b2).
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

  // The first 2 pops serve noisy_items[0] (1000) and noisy_items[1] (1001) from the noisy tenant's
  // two buckets, and the next 10 pops immediately serve all 10 well-behaved tenants (1..10) ahead
  // of noisy_items[2..99].
  EXPECT_EQ(queue.pop().id(), 1000);
  EXPECT_EQ(queue.pop().id(), 1001);
  for (int expected_wb_id = 1; expected_wb_id <= 10; ++expected_wb_id) {
    EXPECT_EQ(queue.pop().id(), expected_wb_id);
  }
  EXPECT_EQ(queue.size(), 98);
  EXPECT_EQ(queue.peek().id(), 1002);
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
