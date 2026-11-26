#pragma once

#include <atomic>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "source/common/common/hash.h"

#include "absl/container/btree_set.h"
#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace FairSieve {

// Hashed representation of a tenant identifier for lock-free atomic hit attribution on worker
// threads. Value 0 is reserved for the default (unattributed) tenant bucket.
//
// Security note: callers must derive tenant names from a proxy-authenticated source (e.g. a
// verified peer identity, connection property, or trusted claim) so an untrusted peer cannot
// spoof another tenant's bucket or craft xxHash64 collisions against a victim tenant.
using TenantId = uint64_t;
inline constexpr TenantId kDefaultTenantId = 0;

// Maps empty names to `kDefaultTenantId` (0) and hashes all non-empty names into
// `[1, UINT64_MAX]`.
// Invariant (sentinel isolation): non-empty tenant names never collide with `kDefaultTenantId`
// because raw hash 0 is remapped to 1.
inline TenantId computeTenantId(absl::string_view tenant_name) noexcept {
  if (tenant_name.empty()) {
    return kDefaultTenantId;
  }
  const uint64_t hash = HashUtil::xxHash64(tenant_name);
  return hash == kDefaultTenantId ? 1 : hash;
}

/**
 * Lock-free per-resource hit metadata shared between worker threads (readers) and the main thread
 * (evictor) for any on-demand resource cache (e.g. on-demand SDS secrets, clusters, scoped routes,
 * DNS records, or external auth caches).
 *
 * Uses test-and-test-and-set (TTAS) relaxed stores so concurrent hits on hot shared resources
 * avoid unnecessary CPU cache-line invalidation traffic across cores.
 *
 * Invariants (under `std::memory_order_relaxed` on 64-bit lock-free atomics):
 *  - Non-clobbering: `recordHit(kDefaultTenantId)` sets `visited_` without overwriting a named
 *    `last_hit_tenant_`.
 *  - Domain safety / anti-ownership-laundering: `last_hit_tenant_` is always either the entry's
 *    initial `owner_tenant` or a named `tenant_id` supplied to `recordHit(tenant_id)` (so an
 *    unprivileged tenant cannot push ownership of its junk entries onto another tenant's queue).
 *  - Benign TTAS lost update: a worker observing `visited_ == true` right before the main thread
 *    clears it in `consumeVisited()` may skip rewriting `true`, trading atomic RMW contention on
 *    hot keys for a rare missed lazy promotion; it never causes corruption or non-termination.
 *  - Post-eviction lifetime safety: because callers hold `std::shared_ptr<const HitState>` in
 *    thread-local snapshots, late worker calls to `recordHit()` after eviction remain memory-safe
 *    and data-race-free until asynchronous snapshot updates drop the last reference.
 */
struct HitState {
  explicit HitState(TenantId owner_tenant = kDefaultTenantId) noexcept
      : last_hit_tenant_(owner_tenant) {}

  void recordHit(TenantId tenant_id) const noexcept {
    if (!visited_.load(std::memory_order_relaxed)) {
      visited_.store(true, std::memory_order_relaxed);
    }
    if (tenant_id != kDefaultTenantId &&
        last_hit_tenant_.load(std::memory_order_relaxed) != tenant_id) {
      last_hit_tenant_.store(tenant_id, std::memory_order_relaxed);
    }
  }

  bool consumeVisited() const noexcept {
    if (!visited_.load(std::memory_order_relaxed)) {
      return false;
    }
    visited_.store(false, std::memory_order_relaxed);
    return true;
  }

  TenantId lastHitTenant() const noexcept {
    return last_hit_tenant_.load(std::memory_order_relaxed);
  }

  mutable std::atomic<bool> visited_{false};
  mutable std::atomic<TenantId> last_hit_tenant_{kDefaultTenantId};
};

using HitStateSharedPtr = std::shared_ptr<const HitState>;

/**
 * General-purpose main-thread policy controller implementing two-phase overload protection for
 * bounded multi-tenant on-demand resource caches:
 *
 * 1. Phase 1 — Pending (warming) admission control (Choudhury & Hahne Dynamic Thresholds, alpha=1):
 *    Bounds in-flight resource fetches waiting for initial delivery (`max_pending`). Before any
 *    named tenant has been observed, the default bucket may use the full budget `max_pending`.
 *    Once any named tenant has been tracked (latched), all tenants—including `kDefaultTenantId`—
 *    are admitted iff:
 *      pending_total < max_pending &&
 *      (tenant_pending == 0 || tenant_pending < max_pending - pending_total)
 *    This keeps a single flooding named tenant at `<= ceil(max_pending / 2)`, converges K
 *    simultaneous flooding tenants to `[floor(max_pending / (K + 1)), ceil(max_pending / (K + 1))]`
 *    pending slots each, and preserves immediate headroom for sparse healthy tenants whenever
 *    global space remains.
 *
 * 2. Phase 2 — Active resource eviction (Work-Conserving Fair-SIEVE with Lazy Ownership Transfer):
 *    Each tenant with at least one tracked resource maintains an intrusive doubly-linked FIFO
 *    queue of its active keys plus a persistent SIEVE eviction hand (Zhang et al., NSDI '24).
 *    Keys are never duplicated across tenants (`sum_t active_size(t) == active_count <= max_active`
 *    and empty tenant buckets are immediately erased), so total policy memory is strictly
 *    `O(max_active + max_pending)`, independent of the number of tenants sharing cached resources.
 *    When `active_count >= max_active`:
 *      a) Inter-tenant max-min fairness: pick a tenant `t*` with the largest active queue size
 *         (via `active_tenants_by_size_` in O(log T)), breaking ties in favor of the incoming
 *         tenant so a tenant already at the fair-share ceiling evicts from itself, and rotating
 *         across other tied tenants via `victim_cursor_` so low-hash tenants cannot stay pinned.
 *      b) Lazy cross-tenant ownership transfer (Non-Blocking FairRide, Pu et al., NSDI '16):
 *         inspect the node at `t*`'s hand. If a different named tenant `last_t` most recently hit
 *         this shared resource and `active_size(last_t) + 1 < active_size(t*)`, splice the node in
 *         O(1) to the head of `last_t`'s queue instead of evicting it. Every transfer strictly
 *         decreases the integer potential `sum_t(active_size(t)^2)` by at least 2 (strictly
 *         preventing ping-pong livelocks and guaranteeing `active_size(t*) >= 1` post-transfer).
 *      c) Intra-tenant SIEVE sweep: otherwise, if `visited` is set, clear it (lazy promotion) and
 *         step `hand` toward head (wrapping to tail); if `visited` is clear, evict the node in O(1)
 *         (quick demotion of one-hit-wonder scans). When `max_size == 1` and the incoming tenant is
 *         not tied at `max_size`, visited singletons receive a SIEVE second chance across the
 *         singleton ring in O(1) per iterator step so hot singleton tenants survive ephemeral
 *         one-shot tenant churn when `T > max_active`.
 *      d) Strict linear worst-case step bound under concurrent worker writes: cumulative transfer
 *         and sweep counters are capped at `initial_active` and `saturatingDouble(initial_active)`
 *         respectively across a single eviction call. By the monotonic peak / no-re-entry property,
 *         sequential runs perform at most `initial_active - 1` transfers and `initial_active`
 *         visited clears, so the caps are 100% transparent sequentially while bounding concurrent
 *         main-thread eviction work to `<= 3 * initial_active + 1` iterations.
 */
class Policy {
public:
  struct EvictionResult {
    std::string evicted_key;
    size_t ownership_transfers{0};
  };

  Policy(size_t max_active, size_t max_pending)
      : max_active_(max_active), max_pending_(max_pending) {}

  // Returns whether a new pending fetch for `tenant_id` is allowed under `max_pending`.
  // Always returns true when `max_pending == 0`. Observing any named tenant
  // (`tenant_id != kDefaultTenantId`) permanently latches multi-tenant dynamic-threshold
  // enforcement for `kDefaultTenantId`, even if this call returns false due to saturation.
  bool canAdmitPending(TenantId tenant_id);

  // Records a newly opened pending (warming) fetch for `key` owned by `tenant_id`.
  // No-op if `key` is already tracked or both limits are disabled.
  void onPendingAdded(absl::string_view key, TenantId tenant_id);

  // Transitions `key` from pending to active and returns its lock-free `HitState` (or nullptr if
  // active eviction is disabled). If `max_active` is already reached, evicts one existing active
  // resource and populates `evicted_out`. If `key` is already active (in-place update/rotation),
  // preserves its queue position and returns its existing hit state.
  HitStateSharedPtr onActivated(absl::string_view key, std::optional<EvictionResult>& evicted_out);

  // Removes `key` (whether pending or active) upon external removal/cancellation.
  // Returns true if the removed entry was in the pending state, false otherwise.
  bool onRemoved(absl::string_view key);

  size_t activeCount() const { return active_count_; }
  size_t pendingCount() const { return pending_count_; }
  size_t tenantCount() const { return tenants_.size(); }
  size_t tenantActiveCount(TenantId tenant_id) const;
  size_t tenantPendingCount(TenantId tenant_id) const;
  size_t maxActive() const { return max_active_; }
  size_t maxPending() const { return max_pending_; }
  bool activeLimitEnabled() const { return max_active_ > 0; }
  bool pendingLimitEnabled() const { return max_pending_ > 0; }
  bool trackingEnabled() const { return max_active_ > 0 || max_pending_ > 0; }

private:
  // Invariant (single-owner link state): each tracked key appears at most once in `entries_`.
  // When `!is_active` (pending), the entry is unlinked (`prev == nullptr && next == nullptr` and
  // `hit_state == nullptr`). When `is_active`, it belongs to the single `TenantQueue` identified
  // by `owner_tenant`.
  struct Entry {
    std::string key;
    TenantId owner_tenant{kDefaultTenantId};
    bool is_active{false};
    HitStateSharedPtr hit_state;
    Entry* prev{nullptr};
    Entry* next{nullptr};
  };

  // Intrusive doubly-linked FIFO queue of a single tenant's active entries with a persistent SIEVE
  // eviction hand (`head` = newest, `tail` = oldest).
  //
  // Class invariants (`InvDLL`, preserved across every `pushHead`, `remove`, and transfer splice):
  //  - Empty consistency: `(active_size == 0) <=> (head == nullptr && tail == nullptr &&
  //    hand == nullptr)`.
  //  - Endpoints: if `active_size > 0`, `head->prev == nullptr` and `tail->next == nullptr`.
  //  - Symmetry & acyclicity: bidirectional links (`u->next == v <=> v->prev == u`) form a single
  //    acyclic chain of exact length `active_size` starting at `head` and ending at `tail`.
  //  - Live hand: `hand == nullptr` or `hand` points to a member of this queue (never dangling).
  struct TenantQueue {
    Entry* head{nullptr};
    Entry* tail{nullptr};
    Entry* hand{nullptr};
    size_t active_size{0};
    size_t pending_size{0};

    void pushHead(Entry* entry) noexcept;
    void remove(Entry* entry) noexcept;
    bool empty() const noexcept { return active_size == 0 && pending_size == 0; }
  };

  EvictionResult evictOne(TenantId incoming_tenant);
  void updateTenantActiveIndex(TenantId tenant_id, size_t old_size, size_t new_size);
  void maybeEraseTenant(TenantId tenant_id);

  const size_t max_active_;
  const size_t max_pending_;

  // Conservation invariants:
  //  - Always (including across `evictOne()`): `active_count_ == sum_{t} tenants_[t].active_size`
  //    (`<= max_active_` when `max_active_ > 0`) and
  //    `pending_count_ == sum_{t} tenants_[t].pending_size`.
  //  - At every public method boundary: `entries_.size() == active_count_ + pending_count_`.
  size_t active_count_{0};
  size_t pending_count_{0};

  // Pointer-stable storage for intrusive doubly-linked per-tenant SIEVE queues.
  absl::flat_hash_map<std::string, std::unique_ptr<Entry>> entries_;
  // Non-empty bucket invariant (enforced at every public method boundary and across `evictOne()`):
  // every `(t, q)` in `tenants_` satisfies `q.active_size + q.pending_size > 0`. Because no queue
  // is ever created on a hit and empty queues are erased by `maybeEraseTenant()`, total buckets
  // satisfy `tenants_.size() <= active_count_ + pending_count_` (strictly `O(max_active +
  // max_pending)` for admission-gated inserts, independent of total tenants sharing entries).
  absl::flat_hash_map<TenantId, TenantQueue> tenants_;

  // Ordered index of tenants with `active_size > 0` keyed by `(active_size, tenant_id)` for
  // O(1) max-tenant lookup and O(log T) updates on insertions, evictions, and transfers.
  // Bijection invariant: `(s, t) in active_tenants_by_size_ <=> (tenants_.contains(t) &&
  // tenants_[t].active_size == s && s > 0)`, so `active_tenants_by_size_.size() <= active_count_`.
  absl::btree_set<std::pair<size_t, TenantId>> active_tenants_by_size_;

  // Rotating cursor used to break non-incoming ties at `max_size` across `active_tenants_by_size_`
  // so low-hash tenants are not permanently pinned when `T > C` or `C % T >= 2`.
  TenantId victim_cursor_{0};

  // Latched once any named (`tenant_id != kDefaultTenantId`) tenant is observed, ensuring
  // `kDefaultTenantId` never re-enables the single-bucket 100% pending bypass in pending-only mode
  // when named tenants momentarily have zero in-flight fetches.
  bool saw_named_tenant_{false};
};

} // namespace FairSieve
} // namespace Envoy
