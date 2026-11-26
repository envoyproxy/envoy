#include "source/common/common/fair_sieve_cache.h"

#include <cstddef>
#include <iterator>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <utility>

#include "source/common/common/assert.h"

namespace Envoy {
namespace FairSieve {

namespace {

// Saturating doubling: prevents unsigned wrap-around on 32-bit `size_t` targets (where `2 * v`
// would wrap at `v >= 0x80000000` and collapse the cumulative sweep guard to 0).
size_t saturatingDouble(size_t value) noexcept {
  constexpr size_t kMax = std::numeric_limits<size_t>::max();
  return value > (kMax / 2) ? kMax : (value * 2);
}

} // namespace

// Preserves `InvDLL`: prepends unlinked `entry` as the new `head` (`head->prev == nullptr`),
// initializing `tail` if empty and leaving `hand` untouched.
void Policy::TenantQueue::pushHead(Entry* entry) noexcept {
  ASSERT(entry != nullptr);
  ASSERT(entry->prev == nullptr && entry->next == nullptr);
  entry->prev = nullptr;
  entry->next = head;
  if (head != nullptr) {
    head->prev = entry;
  } else {
    tail = entry;
  }
  head = entry;
  ++active_size;
}

// Preserves `InvDLL` across head, tail, singleton, and interior removals. Clearing `prev`/`next`
// leaves `entry` ready for either deletion or `pushHead()` onto a peer queue during transfer.
void Policy::TenantQueue::remove(Entry* entry) noexcept {
  ASSERT(entry != nullptr);
  ASSERT(active_size > 0);
  // Advance `hand` toward `head` (or `nullptr` if removing `head`/singleton) so `hand` never
  // dangles after unlinking `entry`.
  if (hand == entry) {
    hand = entry->prev;
  }
  if (entry->prev != nullptr) {
    entry->prev->next = entry->next;
  } else {
    head = entry->next;
  }
  if (entry->next != nullptr) {
    entry->next->prev = entry->prev;
  } else {
    tail = entry->prev;
  }
  entry->prev = nullptr;
  entry->next = nullptr;
  --active_size;
}

bool Policy::canAdmitPending(TenantId tenant_id) {
  if (max_pending_ == 0) {
    return true;
  }
  // Latch multi-tenant mode immediately upon observing any named tenant, even if this arrival is
  // rejected while `kDefaultTenantId` has momentarily saturated `max_pending_`, so subsequent
  // default-bucket arrivals obey dynamic thresholds as soon as pending slots drain.
  if (tenant_id != kDefaultTenantId) {
    saw_named_tenant_ = true;
  }
  // Short-circuit guarantees `pending_count_ < max_pending_` below so unsigned subtraction
  // `max_pending_ - pending_count_` can never underflow (even if prefetched bypasses pushed
  // `pending_count_ > max_pending_`). Also preserves `pending_count_ + 1 <= max_pending_`.
  if (pending_count_ >= max_pending_) {
    return false;
  }
  const auto it = tenants_.find(tenant_id);
  const size_t tenant_pending = (it != tenants_.end()) ? it->second.pending_size : 0;
  // Non-starvation invariant: whenever global headroom remains (`pending_count_ < max_pending_`),
  // an idle tenant's first pending entry is always admitted regardless of peer queue depths.
  if (tenant_pending == 0) {
    return true;
  }
  // In default single-bucket mode (before any named tenant has been observed), allow full
  // utilization of `max_pending_`. Trade-off: in a mixed-mode proxy where unattributed traffic
  // floods *before* the very first named tenant arrives, `kDefaultTenantId` can momentarily fill
  // `max_pending_` until a fetch completes. Invariant: once *any* named tenant has been tracked
  // (`saw_named_tenant_ == true`), `kDefaultTenantId` permanently ceases bypassing and obeys the
  // dynamic threshold below even in pending-only mode when `tenants_` empties between fetches.
  if (tenant_id == kDefaultTenantId && !saw_named_tenant_) {
    return true;
  }
  // Multi-tenant Choudhury & Hahne Dynamic Thresholds (alpha = 1).
  // Strict `<` (NOT `<=`) is required: e.g. at `M = 2, p = 1`, `<=` would let one tenant take 2/2
  // slots and starve an idle peer. Invariants maintained for named tenants:
  //  - Single flooder ceiling: `tenant_pending <= ceil(max_pending_ / 2)`, reserving at least
  //    `floor(max_pending_ / 2)` headroom for all `max_pending_ >= 2`.
  //  - K-flooder steady-state equilibrium: `floor(M/(K+1)) <= p_i <= ceil(M/(K+1))`.
  //  - Monotonicity / self-healing: for `p_a > p_b`, whenever `a` is admissible `b` is also
  //    admissible, so smaller tenants claim drained headroom before saturated flooders.
  return tenant_pending < (max_pending_ - pending_count_);
}

void Policy::onPendingAdded(absl::string_view key, TenantId tenant_id) {
  if (!trackingEnabled()) {
    return;
  }
  if (tenant_id != kDefaultTenantId) {
    saw_named_tenant_ = true;
  }
  if (entries_.contains(key)) {
    return;
  }
  auto entry = std::make_unique<Entry>();
  entry->key = std::string(key);
  entry->owner_tenant = tenant_id;
  entry->is_active = false;
  entries_.emplace(entry->key, std::move(entry));
  ++tenants_[tenant_id].pending_size;
  ++pending_count_;
}

HitStateSharedPtr Policy::onActivated(absl::string_view key,
                                      std::optional<EvictionResult>& evicted_out) {
  evicted_out.reset();
  if (!trackingEnabled()) {
    return nullptr;
  }

  auto it = entries_.find(key);
  if (it == entries_.end()) {
    // If active eviction is disabled, already-active entries are untracked after activation.
    if (!activeLimitEnabled()) {
      return nullptr;
    }
    // Defensive fallback if activation occurred without a prior onPendingAdded().
    auto entry = std::make_unique<Entry>();
    entry->key = std::string(key);
    entry->owner_tenant = kDefaultTenantId;
    entry->is_active = false;
    it = entries_.emplace(entry->key, std::move(entry)).first;
  } else if (it->second->is_active) {
    // In-place resource rotation/update preserves the entry's SIEVE queue position and hit state.
    return it->second->hit_state;
  } else {
    // Transition out of the pending state.
    const TenantId owner = it->second->owner_tenant;
    const auto owner_it = tenants_.find(owner);
    ASSERT(owner_it != tenants_.end());
    TenantQueue& owner_q = owner_it->second;
    ASSERT(owner_q.pending_size > 0);
    ASSERT(pending_count_ > 0);
    --owner_q.pending_size;
    --pending_count_;
    if (!activeLimitEnabled()) {
      entries_.erase(it);
      maybeEraseTenant(owner);
      return nullptr;
    }
    // Maintain strict non-empty tenant bucket invariant across the upcoming `evictOne()` boundary
    // when `owner` just dropped from `active_size == 0, pending_size == 1` to `0 + 0`.
    maybeEraseTenant(owner);
  }

  Entry* entry = it->second.get();
  const TenantId owner = entry->owner_tenant;

  // Work-conservation & self-eviction invariants: `evictOne()` runs strictly iff
  // `active_count_ >= max_active_`. Because `entry` is not yet linked into `owner`'s active queue
  // or `active_tenants_by_size_`, `evictOne()` can never choose `entry` as the victim, and
  // `unique_ptr<Entry>` in `entries_` preserves `entry`'s pointer across any eviction.
  if (active_count_ >= max_active_) {
    evicted_out = evictOne(owner);
  }

  entry->is_active = true;
  entry->hit_state = std::make_shared<HitState>(owner);
  TenantQueue& owner_q = tenants_[owner];
  const size_t old_size = owner_q.active_size;
  owner_q.pushHead(entry);
  updateTenantActiveIndex(owner, old_size, owner_q.active_size);
  ++active_count_;
  return entry->hit_state;
}

bool Policy::onRemoved(absl::string_view key) {
  if (!trackingEnabled()) {
    return false;
  }
  auto it = entries_.find(key);
  if (it == entries_.end()) {
    return false;
  }
  Entry* entry = it->second.get();
  const TenantId owner = entry->owner_tenant;
  const bool was_pending = !entry->is_active;

  const auto q_it = tenants_.find(owner);
  ASSERT(q_it != tenants_.end());
  TenantQueue& q = q_it->second;
  if (entry->is_active) {
    const size_t old_size = q.active_size;
    q.remove(entry);
    updateTenantActiveIndex(owner, old_size, q.active_size);
    ASSERT(active_count_ > 0);
    --active_count_;
  } else {
    ASSERT(q.pending_size > 0);
    ASSERT(pending_count_ > 0);
    --q.pending_size;
    --pending_count_;
  }

  entries_.erase(it);
  maybeEraseTenant(owner);
  return was_pending;
}

size_t Policy::tenantActiveCount(TenantId tenant_id) const {
  const auto it = tenants_.find(tenant_id);
  return (it != tenants_.end()) ? it->second.active_size : 0;
}

size_t Policy::tenantPendingCount(TenantId tenant_id) const {
  const auto it = tenants_.find(tenant_id);
  return (it != tenants_.end()) ? it->second.pending_size : 0;
}

Policy::EvictionResult Policy::evictOne(TenantId incoming_tenant) {
  ASSERT(!active_tenants_by_size_.empty());
  EvictionResult result;

  // Cumulative work guards across the entire eviction call.
  // Inductive "No-Re-Entry into Former Donors" invariant (enabled by max-tenant selection below):
  // let `D` be the set of tenants that have donated >= 1 entry during this call and `M_cur` the
  // current max `active_size`. Every transfer keeps `active_size(u) >= M_cur - 1` for all `u in D`
  // and never increases `M_cur`, so no `u in D` can satisfy the transfer destination guard
  // `last_size + 1 < M_cur` (`u` never receives a transferred entry in the same eviction call).
  // Because each donor retains >= 1 original entry, every sequential run satisfies:
  //   `ownership_transfers <= initial_active - |D| <= initial_active - 1` and
  //   `sweep_steps <= initial_active`.
  // Thus these cumulative caps never alter any sequential execution, while bounding concurrent
  // worst-case iterations to `<= 3 * initial_active + 1` against adversarial worker atomic writes.
  const size_t max_transfers = active_count_;
  const size_t max_sweep_steps = saturatingDouble(active_count_);
  size_t sweep_steps = 0;

  while (!active_tenants_by_size_.empty()) {
    // 1. Inter-tenant max-min fairness: pick a tenant with the largest active queue size.
    //    Break ties in favor of `incoming_tenant` so a tenant at the fair-share ceiling evicts
    //    from itself instead of dipping a peer. For non-incoming ties at `max_size`, rotate using
    //    `victim_cursor_` in O(log T) so low-hash tenants are never permanently pinned when
    //    `T > C` or `C % T >= 2`. Because every picked donor still has `active_size == max_size`,
    //    the No-Re-Entry Lemma and fair-share floor invariants are preserved unchanged.
    const size_t max_size = std::prev(active_tenants_by_size_.end())->first;
    TenantId victim_tenant = kDefaultTenantId;
    if (const auto incoming_it = tenants_.find(incoming_tenant);
        incoming_it != tenants_.end() && incoming_it->second.active_size == max_size) {
      victim_tenant = incoming_tenant;
    } else {
      auto candidate_it = active_tenants_by_size_.lower_bound({max_size, victim_cursor_});
      if (candidate_it == active_tenants_by_size_.end() || candidate_it->first != max_size) {
        candidate_it = active_tenants_by_size_.lower_bound({max_size, 0});
      }
      ASSERT(candidate_it != active_tenants_by_size_.end() && candidate_it->first == max_size);
      // When `max_size == 1` and `incoming_tenant` is not tied at `max_size`, every entry in
      // `active_tenants_by_size_` is a singleton (`first == 1`). Inside a length-1 queue,
      // intra-tenant SIEVE wrap-around would immediately evict a visited entry without inspecting
      // peers; sweeping the singleton ring in O(1) per iterator step gives visited singletons a
      // SIEVE second chance while preserving the cumulative `max_sweep_steps` (`<= 2A`) bound.
      if (max_size == 1) {
        while (sweep_steps < max_sweep_steps) {
          const auto candidate_q = tenants_.find(candidate_it->second);
          ASSERT(candidate_q != tenants_.end() && candidate_q->second.tail != nullptr);
          if (!candidate_q->second.tail->hit_state->consumeVisited()) {
            break;
          }
          ++sweep_steps;
          ++candidate_it;
          if (candidate_it == active_tenants_by_size_.end()) {
            candidate_it = active_tenants_by_size_.begin();
          }
        }
      }
      victim_tenant = candidate_it->second;
    }

    const auto q_it = tenants_.find(victim_tenant);
    ASSERT(q_it != tenants_.end());
    TenantQueue& q = q_it->second;
    Entry* curr = (q.hand != nullptr) ? q.hand : q.tail;
    ASSERT(curr != nullptr);

    while (curr != nullptr) {
      // 2. Lazy cross-tenant ownership transfer (Non-Blocking FairRide):
      //    Strict guard `last_size + 1 < q.active_size` (NOT `last_size < q.active_size`!):
      //    weakening to `<` causes a 2-cycle ping-pong livelock with `delta_Phi == 0` when
      //    `q.active_size - last_size == 1`. With `+ 1 <`:
      //     - `last_size + 1` cannot overflow because `last_size + q.active_size <= active_count_`.
      //     - Strict Lyapunov drop on `Phi = sum_t active_size(t)^2` (bounded in `[A, A^2]`):
      //       `delta_Phi = -2 * (q.active_size - last_size - 1) <= -2`.
      //     - `q.active_size >= 2` before transfer, so `victim_tenant` remains non-empty after
      //       transfer (`q.active_size >= 1`; no `maybeEraseTenant(victim_tenant)` needed).
      if (result.ownership_transfers < max_transfers) {
        const TenantId last_t = curr->hit_state->lastHitTenant();
        if (last_t != victim_tenant && last_t != kDefaultTenantId) {
          saw_named_tenant_ = true;
          const auto last_it = tenants_.find(last_t);
          const size_t last_size = (last_it != tenants_.end()) ? last_it->second.active_size : 0;
          if (last_size + 1 < q.active_size) {
            const size_t old_victim_size = q.active_size;
            q.hand = curr->prev;
            q.remove(curr);
            ASSERT(q.active_size >= 1);
            updateTenantActiveIndex(victim_tenant, old_victim_size, q.active_size);

            // Look up/insert `last_t` only after finishing all mutations on `q` because inserting
            // into `tenants_` may rehash and invalidate references.
            // Cross-queue splice preserves `InvDLL` and disjointness on both `q` and `dst_q`, and
            // conserves `q.active_size + dst_q.active_size` (leaving `active_count_` unchanged).
            TenantQueue& dst_q = tenants_[last_t];
            const size_t old_dst_size = dst_q.active_size;
            curr->owner_tenant = last_t;
            dst_q.pushHead(curr);
            updateTenantActiveIndex(last_t, old_dst_size, dst_q.active_size);

            ++result.ownership_transfers;
            break;
          }
        }
      }

      // 3. Intra-tenant SIEVE sweep: lazy promotion on visited entries, quick demotion on
      //    unvisited entries. Note: even if a shared entry's `last_hit_tenant_` was overwritten
      //    back to `victim_tenant` by a subsequent hit, that hit also set `visited_ = true`,
      //    protecting the entry for one full lap. While `q.active_size > 0`, wrapping from
      //    `head` (`curr->prev == nullptr`) to `q.tail` always yields a live member of `q`.
      if (sweep_steps < max_sweep_steps && curr->hit_state->consumeVisited()) {
        ++sweep_steps;
        curr = (curr->prev != nullptr) ? curr->prev : q.tail;
      } else {
        const size_t old_victim_size = q.active_size;
        q.hand = curr->prev;
        result.evicted_key = curr->key;
        q.remove(curr);
        updateTenantActiveIndex(victim_tenant, old_victim_size, q.active_size);
        ASSERT(active_count_ > 0);
        --active_count_;
        // Dropping `curr->hit_state` releases only the policy's reference; worker-thread snapshots
        // keep their `shared_ptr<const HitState>` alive and race-free until updated.
        entries_.erase(result.evicted_key);
        maybeEraseTenant(victim_tenant);
        // Unsigned wrap-around (`UINT64_MAX + 1 == 0`) naturally wraps back to the band start.
        victim_cursor_ = victim_tenant + 1;
        return result;
      }
    }
  }
  return result;
}

void Policy::updateTenantActiveIndex(TenantId tenant_id, size_t old_size, size_t new_size) {
  if (old_size > 0) {
    active_tenants_by_size_.erase({old_size, tenant_id});
  }
  if (new_size > 0) {
    active_tenants_by_size_.emplace(new_size, tenant_id);
  }
}

void Policy::maybeEraseTenant(TenantId tenant_id) {
  const auto it = tenants_.find(tenant_id);
  if (it != tenants_.end() && it->second.empty()) {
    tenants_.erase(it);
  }
}

} // namespace FairSieve
} // namespace Envoy
