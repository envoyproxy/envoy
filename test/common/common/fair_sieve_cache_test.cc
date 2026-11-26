#include <algorithm>
#include <atomic>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <random>
#include <string>
#include <thread>
#include <vector>

#include "source/common/common/fair_sieve_cache.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/str_cat.h"
#include "gtest/gtest.h"

namespace Envoy {
namespace FairSieve {
namespace {

TEST(TenantIdTest, ComputeTenantId) {
  EXPECT_EQ(kDefaultTenantId, computeTenantId(""));
  const TenantId a = computeTenantId("tenant-a");
  const TenantId b = computeTenantId("tenant-b");
  EXPECT_NE(kDefaultTenantId, a);
  EXPECT_NE(kDefaultTenantId, b);
  EXPECT_NE(a, b);
  EXPECT_EQ(a, computeTenantId("tenant-a"));
}

TEST(HitStateTest, UnattributedHitPreservesNamedLastHitTenant) {
  const TenantId owner = computeTenantId("tenant-owner");
  const TenantId peer = computeTenantId("tenant-peer");
  HitState state(owner);
  EXPECT_EQ(owner, state.lastHitTenant());
  EXPECT_FALSE(state.consumeVisited());

  // Unattributed (default) hit sets visited without clobbering last_hit_tenant_.
  state.recordHit(kDefaultTenantId);
  EXPECT_EQ(owner, state.lastHitTenant());
  EXPECT_TRUE(state.consumeVisited());
  EXPECT_FALSE(state.consumeVisited());

  // Named hit updates both visited and last_hit_tenant_.
  state.recordHit(peer);
  EXPECT_EQ(peer, state.lastHitTenant());
  EXPECT_TRUE(state.consumeVisited());
}

TEST(FairSievePolicyTest, UnboundedIsNoOp) {
  Policy policy(0, 0);
  EXPECT_FALSE(policy.trackingEnabled());
  EXPECT_FALSE(policy.activeLimitEnabled());
  EXPECT_FALSE(policy.pendingLimitEnabled());
  EXPECT_TRUE(policy.canAdmitPending(kDefaultTenantId));

  policy.onPendingAdded("r1", computeTenantId("t1"));
  EXPECT_EQ(0, policy.pendingCount());

  std::optional<Policy::EvictionResult> evicted;
  EXPECT_EQ(nullptr, policy.onActivated("r1", evicted));
  EXPECT_FALSE(evicted.has_value());
  EXPECT_EQ(0, policy.activeCount());
  EXPECT_FALSE(policy.onRemoved("r1"));
}

TEST(FairSievePolicyTest, SingleTenantSieveLazyPromotionAndQuickDemotion) {
  // Capacity 3: single default tenant behaves as standard SIEVE.
  Policy policy(/*max_active=*/3, /*max_pending=*/0);
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("r1", kDefaultTenantId);
  auto h1 = policy.onActivated("r1", evicted);
  ASSERT_NE(nullptr, h1);
  EXPECT_FALSE(evicted.has_value());

  policy.onPendingAdded("r2", kDefaultTenantId);
  auto h2 = policy.onActivated("r2", evicted);
  EXPECT_FALSE(evicted.has_value());

  policy.onPendingAdded("r3", kDefaultTenantId);
  auto h3 = policy.onActivated("r3", evicted);
  EXPECT_FALSE(evicted.has_value());

  // Queue order (head -> tail): [r3, r2, r1].
  // Hit r1 (tail) and r3 (head); leave r2 unvisited.
  h1->recordHit(kDefaultTenantId);
  h3->recordHit(kDefaultTenantId);

  // Inserting r4 sweeps from tail: r1 has visited=true (cleared, hand advances to r2),
  // r2 has visited=false -> r2 is evicted, hand advances to r3.
  policy.onPendingAdded("r4", kDefaultTenantId);
  auto h4 = policy.onActivated("r4", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r2", evicted->evicted_key);
  EXPECT_EQ(0, evicted->ownership_transfers);
  EXPECT_EQ(3, policy.activeCount());

  // Queue order (head -> tail): [r4(v=0), r3(v=1), r1(v=0)], hand at r3.
  // Inserting r5: hand inspects r3 (visited=true -> cleared, advances to r4),
  // r4 has visited=false -> r4 is evicted immediately (quick demotion of one-hit-wonder).
  policy.onPendingAdded("r5", kDefaultTenantId);
  auto h5 = policy.onActivated("r5", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r4", evicted->evicted_key);
  EXPECT_EQ(3, policy.activeCount());

  // Queue order (head -> tail): [r5(v=0), r3(v=0), r1(v=0)], hand wrapped to nullptr -> tail (r1).
  // Inserting r6 evicts r1.
  policy.onPendingAdded("r6", kDefaultTenantId);
  policy.onActivated("r6", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r1", evicted->evicted_key);
  EXPECT_EQ(3, policy.activeCount());
}

TEST(FairSievePolicyTest, MaxActiveOneBoundaryEvictsWithoutTransfer) {
  Policy policy(/*max_active=*/1, /*max_pending=*/1);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  ASSERT_TRUE(policy.canAdmitPending(t_a));
  policy.onPendingAdded("r1", t_a);
  EXPECT_FALSE(policy.canAdmitPending(t_b)); // Global cap reached (M = 1).
  auto h1 = policy.onActivated("r1", evicted);
  EXPECT_FALSE(evicted.has_value());
  EXPECT_EQ(1, policy.activeCount());
  EXPECT_EQ(1, policy.tenantCount());

  // Even after tenant B hits r1, transfer guard `0 + 1 < 1` is false, so r1 is evicted cleanly
  // (after one visited-bit sweep) and tenant A is erased.
  h1->recordHit(t_b);
  ASSERT_TRUE(policy.canAdmitPending(t_b));
  policy.onPendingAdded("r2", t_b);
  policy.onActivated("r2", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r1", evicted->evicted_key);
  EXPECT_EQ(0, evicted->ownership_transfers);
  EXPECT_EQ(1, policy.activeCount());
  EXPECT_EQ(0, policy.tenantActiveCount(t_a));
  EXPECT_EQ(1, policy.tenantActiveCount(t_b));
  EXPECT_EQ(1, policy.tenantCount());
}

TEST(FairSievePolicyTest, MultiTenantMaxMinFairnessAndTieBreaking) {
  // Capacity 2 with 2 tenants: each tenant is protected at its fair share of 1.
  Policy policy(/*max_active=*/2, /*max_pending=*/0);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("b1", t_b);
  policy.onActivated("b1", evicted);
  EXPECT_FALSE(evicted.has_value());

  policy.onPendingAdded("a1", t_a);
  policy.onActivated("a1", evicted);
  EXPECT_FALSE(evicted.has_value());
  EXPECT_EQ(1, policy.tenantActiveCount(t_a));
  EXPECT_EQ(1, policy.tenantActiveCount(t_b));

  // Tenant A floods a2, a3, a4: tie-breaking prefers evicting from incoming tenant A, so B's
  // unvisited entry b1 is never evicted.
  for (int i = 2; i <= 4; ++i) {
    const std::string name = absl::StrCat("a", i);
    policy.onPendingAdded(name, t_a);
    policy.onActivated(name, evicted);
    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(absl::StrCat("a", i - 1), evicted->evicted_key);
    EXPECT_EQ(1, policy.tenantActiveCount(t_a));
    EXPECT_EQ(1, policy.tenantActiveCount(t_b));
  }
}

TEST(FairSievePolicyTest, RateDisparityScanFloodProtectsHealthyTenantsAndOwnWorkingSet) {
  // 10 tenants sharing capacity 20 (fair share = 2 each).
  // Tenant 0 has 1 hot entry ("t0-hot") and floods 200 unique one-shot keys.
  // Tenants 1..9 each hold 2 unvisited entries (never hit again during the flood).
  constexpr size_t kCapacity = 20;
  constexpr size_t kTenants = 10;
  Policy policy(kCapacity, 0);
  std::optional<Policy::EvictionResult> evicted;

  absl::flat_hash_set<std::string> live;
  HitStateSharedPtr t0_hot_state;
  const TenantId t0 = computeTenantId("tenant-0");

  // Populate healthy tenants 1..9 with 2 entries each (18 entries total).
  for (size_t t = 1; t < kTenants; ++t) {
    const TenantId tid = computeTenantId(absl::StrCat("tenant-", t));
    for (int k = 0; k < 2; ++k) {
      const std::string name = absl::StrCat("t", t, "-c", k);
      policy.onPendingAdded(name, tid);
      policy.onActivated(name, evicted);
      EXPECT_FALSE(evicted.has_value());
      live.insert(name);
    }
  }

  // Tenant 0 populates its hot entry and 1 scan entry (reaching capacity 20).
  policy.onPendingAdded("t0-hot", t0);
  t0_hot_state = policy.onActivated("t0-hot", evicted);
  EXPECT_FALSE(evicted.has_value());
  live.insert("t0-hot");

  policy.onPendingAdded("t0-scan-0", t0);
  policy.onActivated("t0-scan-0", evicted);
  EXPECT_FALSE(evicted.has_value());
  live.insert("t0-scan-0");

  // Tenant 0 repeatedly hits t0-hot while flooding 200 unique scan keys.
  for (int i = 1; i <= 200; ++i) {
    t0_hot_state->recordHit(t0);
    const std::string scan = absl::StrCat("t0-scan-", i);
    policy.onPendingAdded(scan, t0);
    policy.onActivated(scan, evicted);
    ASSERT_TRUE(evicted.has_value());
    live.erase(evicted->evicted_key);
    live.insert(scan);
  }

  EXPECT_EQ(kCapacity, policy.activeCount());
  // All 18 healthy tenant entries must still be present despite 0 hits during the flood.
  for (size_t t = 1; t < kTenants; ++t) {
    const TenantId tid = computeTenantId(absl::StrCat("tenant-", t));
    EXPECT_EQ(2, policy.tenantActiveCount(tid));
    EXPECT_TRUE(live.contains(absl::StrCat("t", t, "-c0")));
    EXPECT_TRUE(live.contains(absl::StrCat("t", t, "-c1")));
  }
  // Tenant 0's legitimate hot entry also survived its own scan flood (intra-tenant SIEVE).
  EXPECT_EQ(2, policy.tenantActiveCount(t0));
  EXPECT_TRUE(live.contains("t0-hot"));
}

TEST(FairSievePolicyTest, FirstMinterPoisoningRescuedByLazyOwnershipTransfer) {
  // Capacity 4: Tenant A (noisy) initially populates a shared hot key "shared.example" plus 3
  // noise keys. Tenant B (sparse, 0 active keys) hits "shared.example". When Tenant A floods
  // another key, "shared.example" is transferred in O(1) from Tenant A to Tenant B instead of
  // being evicted, and one of Tenant A's unvisited noise keys is evicted instead.
  Policy policy(/*max_active=*/4, /*max_pending=*/0);
  const TenantId t_a = computeTenantId("noisy-a");
  const TenantId t_b = computeTenantId("sparse-b");
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("shared.example", t_a);
  auto shared_state = policy.onActivated("shared.example", evicted);
  ASSERT_NE(nullptr, shared_state);

  for (int i = 1; i <= 3; ++i) {
    const std::string name = absl::StrCat("noise-", i);
    policy.onPendingAdded(name, t_a);
    policy.onActivated(name, evicted);
    EXPECT_FALSE(evicted.has_value());
  }
  EXPECT_EQ(4, policy.tenantActiveCount(t_a));
  EXPECT_EQ(0, policy.tenantActiveCount(t_b));

  // Tenant B hits the shared entry.
  shared_state->recordHit(t_b);

  // Tenant A floods noise-4: hand is at tail ("shared.example"), sees last_hit_tenant == t_b and
  // active_size(t_b) + 1 = 1 < active_size(t_a) = 4 -> transfers ownership to t_b, then evicts
  // noise-1 from t_a.
  policy.onPendingAdded("noise-4", t_a);
  policy.onActivated("noise-4", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("noise-1", evicted->evicted_key);
  EXPECT_EQ(1, evicted->ownership_transfers);
  EXPECT_EQ(3, policy.tenantActiveCount(t_a));
  EXPECT_EQ(1, policy.tenantActiveCount(t_b));

  // Further flooding by Tenant A cannot touch "shared.example" now owned by Tenant B.
  for (int i = 5; i <= 15; ++i) {
    const std::string name = absl::StrCat("noise-", i);
    policy.onPendingAdded(name, t_a);
    policy.onActivated(name, evicted);
    ASSERT_TRUE(evicted.has_value());
    EXPECT_NE("shared.example", evicted->evicted_key);
    EXPECT_EQ(1, policy.tenantActiveCount(t_b));
  }
}

TEST(FairSievePolicyTest, SequentialTransferCascadeRespectsNoReentryBound) {
  // Formal verification edge case (capacity A = 6):
  // T0 initially holds 5 entries hit by distinct peers (two by T1, one by T2, one by T3, one by
  // T4), and T1 holds 1 entry hit by T0. Because max-tenant selection prevents re-entry into
  // former donors, a single sequential eviction performs at most A - 1 transfers (strictly < 6).
  Policy policy(/*max_active=*/6, /*max_pending=*/0);
  const TenantId t0 = computeTenantId("t0");
  const TenantId t1 = computeTenantId("t1");
  const TenantId t2 = computeTenantId("t2");
  const TenantId t3 = computeTenantId("t3");
  const TenantId t4 = computeTenantId("t4");
  const TenantId t5 = computeTenantId("t5");
  std::optional<Policy::EvictionResult> evicted;

  auto add = [&](const std::string& k, TenantId owner, TenantId hitter) {
    policy.onPendingAdded(k, owner);
    auto st = policy.onActivated(k, evicted);
    ASSERT_FALSE(evicted.has_value());
    st->recordHit(hitter);
  };
  add("n0", t0, t1);
  add("n1", t0, t1);
  add("n2", t0, t2);
  add("n3", t0, t3);
  add("n5", t0, t4);
  add("n4", t1, t0);

  policy.onPendingAdded("trigger", t5);
  policy.onActivated("trigger", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_LT(evicted->ownership_transfers, 6);
  EXPECT_EQ(6, policy.activeCount());
  EXPECT_LE(policy.tenantCount(), 6);
}

TEST(FairSievePolicyTest, ZeroActiveOwnerPendingActivationPreservesMaxMinFairness) {
  // When tenant B holds 0 active entries and 1 pending entry, activating `b1` at full capacity
  // transitions `b`'s pending count to 0 (and erases its empty bucket before entering
  // `evictOne()`). After transferring shared `a1` from A (size 2) to B (size 0 -> 1), A and B tie
  // at max_size=1. Tie-breaking selects incoming tenant B to evict from itself (`a1`), leaving
  // exact 1:1 max-min fairness (`a2` for A, `b1` for B) instead of starving A to 0.
  Policy policy(/*max_active=*/2, /*max_pending=*/2);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("a1", t_a);
  auto h1 = policy.onActivated("a1", evicted);
  policy.onPendingAdded("a2", t_a);
  policy.onActivated("a2", evicted);
  EXPECT_EQ(1, policy.tenantCount());

  // Tenant B hits a1 (making it eligible for lazy transfer from A(2) -> B(0)).
  h1->recordHit(t_b);
  policy.onPendingAdded("b1", t_b);
  EXPECT_EQ(2, policy.tenantCount());
  EXPECT_EQ(1, policy.tenantPendingCount(t_b));

  policy.onActivated("b1", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("a1", evicted->evicted_key);
  EXPECT_EQ(1, evicted->ownership_transfers);
  EXPECT_EQ(2, policy.activeCount());
  EXPECT_EQ(0, policy.pendingCount());
  EXPECT_EQ(1, policy.tenantActiveCount(t_a));
  EXPECT_EQ(1, policy.tenantActiveCount(t_b));
  EXPECT_EQ(2, policy.tenantCount());
}

TEST(FairSievePolicyTest, ZeroActiveOwnerPendingActivationWithoutTransferDropsEmptyBucket) {
  // With capacity 3, A holds 3 entries (a1 hit by B). Activating `b1` erases B's empty pending
  // bucket, transfers `a1` to B (A: 3->2, B: 0->1), then evicts `a2` from A (still strict max!).
  Policy policy(/*max_active=*/3, /*max_pending=*/2);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("a1", t_a);
  auto h1 = policy.onActivated("a1", evicted);
  policy.onPendingAdded("a2", t_a);
  policy.onActivated("a2", evicted);
  policy.onPendingAdded("a3", t_a);
  policy.onActivated("a3", evicted);

  h1->recordHit(t_b);
  policy.onPendingAdded("b1", t_b);
  policy.onActivated("b1", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("a2", evicted->evicted_key);
  EXPECT_EQ(1, evicted->ownership_transfers);
  EXPECT_EQ(3, policy.activeCount());
  EXPECT_EQ(0, policy.pendingCount());
  EXPECT_EQ(1, policy.tenantActiveCount(t_a));
  EXPECT_EQ(2, policy.tenantActiveCount(t_b));
  EXPECT_EQ(2, policy.tenantCount());
}

TEST(FairSievePolicyTest, InPlaceRotationPreservesHitStateAndQueuePosition) {
  Policy policy(/*max_active=*/2, /*max_pending=*/0);
  const TenantId t_a = computeTenantId("tenant-a");
  std::optional<Policy::EvictionResult> evicted;

  policy.onPendingAdded("r1", t_a);
  auto h1 = policy.onActivated("r1", evicted);
  policy.onPendingAdded("r2", t_a);
  auto h2 = policy.onActivated("r2", evicted);

  h1->recordHit(t_a);

  // In-place update for r1 should return the exact same HitState, trigger no eviction, and keep
  // visited=true.
  auto h1_rotated = policy.onActivated("r1", evicted);
  EXPECT_EQ(h1, h1_rotated);
  EXPECT_FALSE(evicted.has_value());
  EXPECT_EQ(2, policy.activeCount());

  // Inserting r3 should skip r1 (still visited) and evict r2.
  policy.onPendingAdded("r3", t_a);
  policy.onActivated("r3", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r2", evicted->evicted_key);
}

TEST(FairSievePolicyTest, ExternalRemovalAdjustsHandAndCleansUpEmptyTenants) {
  Policy policy(/*max_active=*/3, /*max_pending=*/3);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  // Pending removal.
  policy.onPendingAdded("p1", t_b);
  EXPECT_EQ(1, policy.pendingCount());
  EXPECT_EQ(1, policy.tenantPendingCount(t_b));
  EXPECT_TRUE(policy.onRemoved("p1"));
  EXPECT_EQ(0, policy.pendingCount());
  EXPECT_EQ(0, policy.tenantCount());

  // Active removal when SIEVE hand points to the removed entry.
  policy.onPendingAdded("r1", t_a);
  auto h1 = policy.onActivated("r1", evicted);
  policy.onPendingAdded("r2", t_a);
  auto h2 = policy.onActivated("r2", evicted);
  policy.onPendingAdded("r3", t_a);
  policy.onActivated("r3", evicted);

  // Hit r1, insert r4 -> evicts r2, leaving hand pointing at r3.
  h1->recordHit(t_a);
  policy.onPendingAdded("r4", t_a);
  policy.onActivated("r4", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("r2", evicted->evicted_key);

  // Remove r3 externally (currently pointed to by hand).
  EXPECT_FALSE(policy.onRemoved("r3"));
  EXPECT_EQ(2, policy.activeCount());
  EXPECT_EQ(2, policy.tenantActiveCount(t_a));

  // Add r5 (fills to 3) and r6 (evicts cleanly without dangling hand pointer).
  policy.onPendingAdded("r5", t_a);
  policy.onActivated("r5", evicted);
  EXPECT_FALSE(evicted.has_value());
  policy.onPendingAdded("r6", t_a);
  policy.onActivated("r6", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ(3, policy.activeCount());

  // Remove all remaining entries and verify tenant is erased.
  for (const auto& name : {"r1", "r4", "r5", "r6"}) {
    policy.onRemoved(name);
  }
  EXPECT_EQ(0, policy.activeCount());
  EXPECT_EQ(0, policy.tenantCount());
}

TEST(FairSievePolicyTest, PendingAdmissionDefaultSingleBucketUsesFullBudget) {
  Policy policy(/*max_active=*/0, /*max_pending=*/3);
  EXPECT_TRUE(policy.pendingLimitEnabled());
  EXPECT_FALSE(policy.activeLimitEnabled());

  for (int i = 0; i < 3; ++i) {
    ASSERT_TRUE(policy.canAdmitPending(kDefaultTenantId));
    policy.onPendingAdded(absl::StrCat("r", i), kDefaultTenantId);
  }
  EXPECT_EQ(3, policy.pendingCount());
  EXPECT_FALSE(policy.canAdmitPending(kDefaultTenantId));

  // Activating one pending entry frees a slot (and untracks it since max_active == 0).
  std::optional<Policy::EvictionResult> evicted;
  EXPECT_EQ(nullptr, policy.onActivated("r0", evicted));
  EXPECT_FALSE(evicted.has_value());
  EXPECT_EQ(2, policy.pendingCount());
  EXPECT_EQ(0, policy.activeCount());
  EXPECT_TRUE(policy.canAdmitPending(kDefaultTenantId));
}

TEST(FairSievePolicyTest, PendingAdmissionMixedModeDefaultTransitionsToDynamicThreshold) {
  // As soon as a named tenant is present (`tenants_.size() >= 2`), even `kDefaultTenantId` must
  // obey the alpha=1 dynamic threshold rule rather than claiming 100% of pending capacity.
  Policy policy(/*max_active=*/0, /*max_pending=*/10);
  const TenantId named = computeTenantId("named-tenant");

  ASSERT_TRUE(policy.canAdmitPending(named));
  policy.onPendingAdded("n0", named);

  size_t default_admitted = 0;
  for (int i = 0; i < 10; ++i) {
    if (policy.canAdmitPending(kDefaultTenantId)) {
      policy.onPendingAdded(absl::StrCat("d", i), kDefaultTenantId);
      ++default_admitted;
    }
  }
  // With 1 slot held by `named`, remaining is 9 -> default admits d0..d4 (5 slots total:
  // 0, 1<8, 2<7, 3<6, 4<5, rejects 5<4).
  EXPECT_EQ(5, default_admitted);
  EXPECT_EQ(6, policy.pendingCount());
  EXPECT_FALSE(policy.canAdmitPending(kDefaultTenantId));
  // Another idle named tenant still has immediate headroom (4 free slots remain).
  EXPECT_TRUE(policy.canAdmitPending(computeTenantId("idle-tenant")));
}

TEST(FairSievePolicyTest, PendingAdmissionMultiTenantDynamicThresholdsReservesHeadroom) {
  // With M = 10 and alpha = 1:
  // - Single named tenant A on a cold proxy can claim at most 5 slots (tenant_pending < M - total
  //   admits 0<10, 1<9, 2<8, 3<7, 4<6, then rejects 5<5), leaving 5 slots of headroom.
  // - Second named tenant B can then claim 3 slots (0<5, 1<4, 2<3, then rejects 3<2).
  // - Sparse idle tenants C and D each still get guaranteed admission for their first slot.
  Policy policy(/*max_active=*/0, /*max_pending=*/10);
  const TenantId t_a = computeTenantId("tenant-a");
  const TenantId t_b = computeTenantId("tenant-b");
  const TenantId t_c = computeTenantId("tenant-c");
  const TenantId t_d = computeTenantId("tenant-d");

  size_t a_admitted = 0;
  for (int i = 0; i < 10; ++i) {
    if (policy.canAdmitPending(t_a)) {
      policy.onPendingAdded(absl::StrCat("a", i), t_a);
      ++a_admitted;
    }
  }
  EXPECT_EQ(5, a_admitted);
  EXPECT_EQ(5, policy.pendingCount());
  EXPECT_FALSE(policy.canAdmitPending(t_a));

  size_t b_admitted = 0;
  for (int i = 0; i < 10; ++i) {
    if (policy.canAdmitPending(t_b)) {
      policy.onPendingAdded(absl::StrCat("b", i), t_b);
      ++b_admitted;
    }
  }
  EXPECT_EQ(3, b_admitted);
  EXPECT_EQ(8, policy.pendingCount());

  // Idle tenants C and D are still admitted for their first pending entry.
  EXPECT_TRUE(policy.canAdmitPending(t_c));
  policy.onPendingAdded("c0", t_c);
  EXPECT_FALSE(policy.canAdmitPending(t_c));

  EXPECT_TRUE(policy.canAdmitPending(t_d));
  policy.onPendingAdded("d0", t_d);
  EXPECT_EQ(10, policy.pendingCount());

  // Global cap reached: even a brand-new tenant E is rejected until a slot frees up.
  EXPECT_FALSE(policy.canAdmitPending(computeTenantId("tenant-e")));
}

TEST(FairSievePolicyTest, RandomizedStateMachineInvariants) {
  // Exercises 10,000 interleaved pending/activate/rotate/hit/remove steps verifying strict count
  // conservation, `tenantCount() <= active + pending`, and sequential transfer bound `< active`.
  constexpr size_t kMaxActive = 8;
  constexpr size_t kMaxPending = 6;
  Policy policy(kMaxActive, kMaxPending);

  std::vector<TenantId> tenants = {kDefaultTenantId};
  for (int i = 1; i <= 5; ++i) {
    tenants.push_back(computeTenantId(absl::StrCat("fuzz-t", i)));
  }
  std::vector<HitStateSharedPtr> tracked_states(20);
  absl::flat_hash_set<std::string> active_keys;
  absl::flat_hash_set<std::string> pending_keys;

  std::mt19937_64 rng(0x5eed1234ULL);
  for (int step = 0; step < 10000; ++step) {
    const size_t key_idx = rng() % tracked_states.size();
    const std::string key = absl::StrCat("k", key_idx);
    const TenantId tid = tenants[rng() % tenants.size()];
    const uint64_t op = rng() % 10;

    if (op < 3) {
      if (policy.canAdmitPending(tid) && !active_keys.contains(key) &&
          !pending_keys.contains(key)) {
        policy.onPendingAdded(key, tid);
        pending_keys.insert(key);
      }
    } else if (op < 6) {
      std::optional<Policy::EvictionResult> evicted;
      const size_t active_before = policy.activeCount();
      if (pending_keys.contains(key)) {
        pending_keys.erase(key);
        tracked_states[key_idx] = policy.onActivated(key, evicted);
        active_keys.insert(key);
        if (evicted.has_value()) {
          EXPECT_LT(evicted->ownership_transfers, active_before);
          active_keys.erase(evicted->evicted_key);
        }
      } else if (active_keys.contains(key)) {
        // In-place rotation.
        auto rotated = policy.onActivated(key, evicted);
        EXPECT_EQ(tracked_states[key_idx], rotated);
        EXPECT_FALSE(evicted.has_value());
      }
    } else if (op < 8) {
      if (active_keys.contains(key) && tracked_states[key_idx] != nullptr) {
        tracked_states[key_idx]->recordHit(tid);
      }
    } else {
      const bool was_pending = policy.onRemoved(key);
      if (pending_keys.contains(key)) {
        EXPECT_TRUE(was_pending);
        pending_keys.erase(key);
      } else if (active_keys.contains(key)) {
        EXPECT_FALSE(was_pending);
        active_keys.erase(key);
        tracked_states[key_idx].reset();
      } else {
        EXPECT_FALSE(was_pending);
      }
    }

    EXPECT_EQ(active_keys.size(), policy.activeCount());
    EXPECT_EQ(pending_keys.size(), policy.pendingCount());
    EXPECT_LE(policy.activeCount(), kMaxActive);
    EXPECT_LE(policy.pendingCount(), kMaxPending);
    EXPECT_LE(policy.tenantCount(), policy.activeCount() + policy.pendingCount());

    size_t sum_active = 0;
    size_t sum_pending = 0;
    for (TenantId t : tenants) {
      sum_active += policy.tenantActiveCount(t);
      sum_pending += policy.tenantPendingCount(t);
    }
    EXPECT_EQ(policy.activeCount(), sum_active);
    EXPECT_EQ(policy.pendingCount(), sum_pending);
  }
}

TEST(FairSievePolicyTest, ConcurrentWorkerHitsUnderActiveEvictionAreRaceFreeAndTerminate) {
  // Exercises lock-free HitState updates from worker threads concurrently with main-thread
  // Policy eviction and lazy ownership transfer (including the cumulative transfer/sweep guards).
  constexpr size_t kCapacity = 8;
  Policy policy(kCapacity, 0);
  std::optional<Policy::EvictionResult> evicted;

  std::vector<TenantId> tenants;
  for (int t = 0; t < 4; ++t) {
    tenants.push_back(computeTenantId(absl::StrCat("worker-tenant-", t)));
  }

  std::vector<HitStateSharedPtr> initial_states;
  for (size_t i = 0; i < kCapacity; ++i) {
    const std::string name = absl::StrCat("init-", i);
    policy.onPendingAdded(name, tenants[i % tenants.size()]);
    initial_states.push_back(policy.onActivated(name, evicted));
  }

  std::atomic<bool> stop{false};
  std::vector<std::thread> workers;
  for (int w = 0; w < 4; ++w) {
    workers.emplace_back([&, w]() {
      size_t iter = 0;
      while (!stop.load(std::memory_order_relaxed)) {
        const auto& st = initial_states[(iter + w) % initial_states.size()];
        st->recordHit(tenants[(iter + w) % tenants.size()]);
        ++iter;
      }
    });
  }

  for (int i = 0; i < 200; ++i) {
    const std::string name = absl::StrCat("churn-", i);
    policy.onPendingAdded(name, tenants[i % tenants.size()]);
    policy.onActivated(name, evicted);
    ASSERT_TRUE(evicted.has_value());
    EXPECT_LE(evicted->ownership_transfers, kCapacity);
    EXPECT_EQ(kCapacity, policy.activeCount());
  }

  stop.store(true, std::memory_order_relaxed);
  for (auto& w : workers) {
    w.join();
  }
}

std::vector<TenantId> sortedTenants(size_t count) {
  std::vector<TenantId> tenants;
  tenants.reserve(count);
  for (size_t i = 0; i < count; ++i) {
    tenants.push_back(computeTenantId(absl::StrCat("sim-tenant-", i)));
  }
  std::sort(tenants.begin(), tenants.end());
  return tenants;
}

TEST(FairSievePolicyTest, RotatingTieBreakingAvoidsHighHashStarvationWhenTExceedsC) {
  constexpr size_t kCapacity = 4;
  constexpr size_t kTenants = 8;
  Policy policy(kCapacity, /*max_pending=*/0);
  const std::vector<TenantId> tenants = sortedTenants(kTenants);
  std::optional<Policy::EvictionResult> evicted;

  // Populate the C - 1 lowest-hash tenants (which then stay completely silent) plus one peer.
  for (size_t i = 0; i < kCapacity; ++i) {
    const std::string key = absl::StrCat("k-t", i);
    policy.onPendingAdded(key, tenants[i]);
    ASSERT_NE(nullptr, policy.onActivated(key, evicted));
    ASSERT_FALSE(evicted.has_value());
  }

  // Higher-hash tenants continuously activate and hit keys; idle low-hash tenants must be evicted
  // rather than remaining pinned forever by static max-TenantId tie-breaking.
  size_t idle_low_hash_evictions = 0;
  for (int round = 0; round < 50; ++round) {
    for (size_t i = kCapacity - 1; i < kTenants; ++i) {
      const std::string key = absl::StrCat("k-t", i, "-r", round);
      policy.onPendingAdded(key, tenants[i]);
      auto hit = policy.onActivated(key, evicted);
      ASSERT_NE(nullptr, hit);
      hit->recordHit(tenants[i]);
      ASSERT_TRUE(evicted.has_value());
      if (evicted->evicted_key == "k-t0" || evicted->evicted_key == "k-t1" ||
          evicted->evicted_key == "k-t2") {
        ++idle_low_hash_evictions;
      }
    }
  }

  EXPECT_EQ(3, idle_low_hash_evictions);
  size_t live_slots = 0;
  for (size_t i = kCapacity - 1; i < kTenants; ++i) {
    live_slots += policy.tenantActiveCount(tenants[i]);
  }
  EXPECT_EQ(kCapacity, live_slots);
}

TEST(FairSievePolicyTest, RotatingTieBreakingEliminatesRemainderSlotBiasWhenTLeqC) {
  constexpr size_t kCapacity = 6;
  constexpr size_t kTenants = 4; // Fair share = 1 with 2 remainder slots (r = 2).
  Policy policy(kCapacity, /*max_pending=*/0);
  const std::vector<TenantId> tenants = sortedTenants(kTenants);
  std::optional<Policy::EvictionResult> evicted;
  absl::flat_hash_set<std::string> live;

  std::vector<size_t> total_occupancy(kTenants, 0);
  constexpr int kRounds = 100;
  for (int round = 0; round < kRounds; ++round) {
    for (size_t t = 0; t < kTenants; ++t) {
      const std::string key = absl::StrCat("t", t, "-k", round % 2);
      if (!live.contains(key)) {
        policy.onPendingAdded(key, tenants[t]);
        policy.onActivated(key, evicted);
        live.insert(key);
        if (evicted.has_value()) {
          live.erase(evicted->evicted_key);
        }
      }
      for (size_t i = 0; i < kTenants; ++i) {
        total_occupancy[i] += policy.tenantActiveCount(tenants[i]);
      }
    }
  }

  const double lowest_avg = static_cast<double>(total_occupancy.front()) / (kRounds * kTenants);
  const double highest_avg = static_cast<double>(total_occupancy.back()) / (kRounds * kTenants);
  EXPECT_NEAR(lowest_avg, highest_avg, 0.15);
}

TEST(FairSievePolicyTest, PendingOnlyModeLatchesMultiTenantModeAcrossDrains) {
  constexpr size_t kMaxPending = 10;
  Policy policy(/*max_active=*/0, kMaxPending);
  const TenantId named_a = computeTenantId("named-tenant-a");
  const TenantId named_b = computeTenantId("named-tenant-b");
  std::optional<Policy::EvictionResult> evicted;

  // Named tenant A fetches and completes one secret; in pending-only mode its bucket is erased.
  ASSERT_TRUE(policy.canAdmitPending(named_a));
  policy.onPendingAdded("n1", named_a);
  EXPECT_EQ(nullptr, policy.onActivated("n1", evicted));
  ASSERT_EQ(0, policy.pendingCount());
  ASSERT_EQ(0, policy.tenantCount());

  // Subsequent unattributed traffic must stay capped at <= ceil(M/2) rather than re-enabling
  // the single-bucket 100% bypass.
  size_t default_admitted = 0;
  for (size_t i = 0; i < kMaxPending; ++i) {
    if (policy.canAdmitPending(kDefaultTenantId)) {
      policy.onPendingAdded(absl::StrCat("def-", i), kDefaultTenantId);
      ++default_admitted;
    }
  }
  EXPECT_LE(default_admitted, kMaxPending / 2);
  EXPECT_TRUE(policy.canAdmitPending(named_b));
}

TEST(FairSievePolicyTest, PendingOnlyModeLatchesMultiTenantModeOnRejectedNamedArrival) {
  // Even if `kDefaultTenantId` fills `M/M` slots before any named tenant arrives, the first
  // rejected `canAdmitPending(named)` call must latch multi-tenant mode so default traffic cannot
  // reclaim all drained slots and continue starving named tenants.
  constexpr size_t kMaxPending = 4;
  Policy policy(/*max_active=*/0, kMaxPending);
  const TenantId named = computeTenantId("named-tenant");
  std::optional<Policy::EvictionResult> evicted;

  for (size_t i = 0; i < kMaxPending; ++i) {
    ASSERT_TRUE(policy.canAdmitPending(kDefaultTenantId));
    policy.onPendingAdded(absl::StrCat("def-", i), kDefaultTenantId);
  }
  ASSERT_EQ(kMaxPending, policy.pendingCount());

  // Saturated: named arrival is rejected, but observing it latches `saw_named_tenant_`.
  EXPECT_FALSE(policy.canAdmitPending(named));

  // Drain two default slots (`pending_count == 2`, `default_pending == 2`).
  // With the latch active, `2 < 4 - 2` is false, so default is capped and headroom is preserved.
  EXPECT_EQ(nullptr, policy.onActivated("def-0", evicted));
  EXPECT_EQ(nullptr, policy.onActivated("def-1", evicted));
  EXPECT_FALSE(policy.canAdmitPending(kDefaultTenantId));
  EXPECT_TRUE(policy.canAdmitPending(named));
}

TEST(FairSievePolicyTest, SingletonEvictionRotatesRoundRobinAndSkipsRedundantVisitedConsume) {
  // When `max_size == 1`, both non-incoming band rotation and incoming self-eviction evict the
  // sole entry of the chosen tenant directly in O(1) without redundant `consumeVisited()` calls.
  constexpr size_t kCapacity = 4;
  Policy policy(kCapacity, /*max_pending=*/0);
  const std::vector<TenantId> tenants = sortedTenants(kCapacity + 2);
  std::optional<Policy::EvictionResult> evicted;

  std::vector<HitStateSharedPtr> states;
  states.reserve(kCapacity);
  for (size_t i = 0; i < kCapacity; ++i) {
    const std::string key = absl::StrCat("s-", i);
    policy.onPendingAdded(key, tenants[i]);
    auto hs = policy.onActivated(key, evicted);
    ASSERT_NE(nullptr, hs);
    ASSERT_FALSE(evicted.has_value());
    hs->recordHit(tenants[i]);
    states.push_back(std::move(hs));
  }

  // Non-incoming eviction at `max_size == 1`: evicts tenants[0] directly via `victim_cursor_`
  // without touching `visited_` on either the evicted entry or its peers.
  policy.onPendingAdded("incoming-4", tenants[4]);
  auto hs4 = policy.onActivated("incoming-4", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("s-0", evicted->evicted_key);
  EXPECT_EQ(0, evicted->ownership_transfers);
  for (const auto& hs : states) {
    EXPECT_TRUE(hs->visited_.load(std::memory_order_relaxed));
  }

  // Next non-incoming arrival advances `victim_cursor_` to tenants[1].
  policy.onPendingAdded("incoming-5", tenants[5]);
  policy.onActivated("incoming-5", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("s-1", evicted->evicted_key);

  // Self-eviction at `max_size == 1`: tenants[4] evicts its own visited entry directly without
  // touching peers or performing a redundant `consumeVisited()`.
  hs4->recordHit(tenants[4]);
  ASSERT_TRUE(hs4->visited_.load(std::memory_order_relaxed));
  policy.onPendingAdded("incoming-4-second", tenants[4]);
  policy.onActivated("incoming-4-second", evicted);
  ASSERT_TRUE(evicted.has_value());
  EXPECT_EQ("incoming-4", evicted->evicted_key);
  EXPECT_TRUE(hs4->visited_.load(std::memory_order_relaxed));
  EXPECT_EQ(1, policy.tenantActiveCount(tenants[4]));
  EXPECT_EQ(1, policy.tenantActiveCount(tenants[2]));
  EXPECT_EQ(1, policy.tenantActiveCount(tenants[3]));
  EXPECT_EQ(1, policy.tenantActiveCount(tenants[5]));
}

TEST(FairSievePolicyTest, SelfEvictingFlooderDoesNotClobberVictimCursorAndPinIdlePeers) {
  constexpr size_t kCapacity = 3;
  Policy policy(kCapacity, /*max_pending=*/0);
  const std::vector<TenantId> tenants = sortedTenants(4); // t0 < t1 < t2 < t3
  std::optional<Policy::EvictionResult> evicted;

  // Initial state: flooder t0, active peer t1, and permanently idle peer t3 occupy 3/3 slots.
  policy.onPendingAdded("t0-init", tenants[0]);
  policy.onActivated("t0-init", evicted);
  policy.onPendingAdded("t1-init", tenants[1]);
  policy.onActivated("t1-init", evicted);
  policy.onPendingAdded("t3-idle", tenants[3]);
  policy.onActivated("t3-idle", evicted);
  ASSERT_FALSE(evicted.has_value());

  // Between every non-incoming arrival from {t1, t2}, t0 continuously self-evicts. Self-evictions
  // must NOT reset `victim_cursor_` to `t0 + 1` (which would ping-pong t1/t2 and pin t3 forever).
  bool idle_t3_evicted = false;
  for (int round = 0; round < 10 && !idle_t3_evicted; ++round) {
    const std::string flood_key = absl::StrCat("t0-flood-", round);
    policy.onPendingAdded(flood_key, tenants[0]);
    policy.onActivated(flood_key, evicted);
    ASSERT_TRUE(evicted.has_value());
    EXPECT_EQ(1, policy.tenantActiveCount(tenants[0]));

    const TenantId absent_peer =
        (policy.tenantActiveCount(tenants[2]) == 0) ? tenants[2] : tenants[1];
    const std::string peer_key = absl::StrCat("peer-", round);
    policy.onPendingAdded(peer_key, absent_peer);
    policy.onActivated(peer_key, evicted);
    ASSERT_TRUE(evicted.has_value());
    if (evicted->evicted_key == "t3-idle") {
      idle_t3_evicted = true;
    }
  }
  EXPECT_TRUE(idle_t3_evicted);
  EXPECT_EQ(0, policy.tenantActiveCount(tenants[3]));
}

} // namespace
} // namespace FairSieve
} // namespace Envoy
