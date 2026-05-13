#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/common/token_bucket.h"

#include "source/common/common/thread.h"

#include "absl/container/flat_hash_set.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

/**
 * The structure here is conceptually cascade of buckets.
 *
 *                Bucket
 *                   |
 *        +------+---+------------+
 *        |      |                |
 *    Tenant1  Tenant2          Tenant3
 *     |        |                 |
 *  Request   Request       +-----+---+------+
 *                          |         |      |
 *                     Request    Request   Request
 *
 * The interface for actual use is `Client` which provides
 * [the relevant part of] the TokenBucket interface. This allows
 * RAII to cancel requests for tokens.
 *
 * The actual implementation represents the structure depicted
 * above not as a flow of tokens, but as a way of calculating the
 * weight of each client for token distribution - in the diagram
 * above, if all the requests are being limited, the request under
 * Tenant1 will receive Tenant1Weight/SumTenantWeights of the
 * available tokens, the request under Tenant2 will receive
 * Tenant2Weight/SumTenantWeights, and the requests under Tenant3,
 * because there are three of them, will receive
 * Tenant3Weight/SumTenantWeights/3.
 */

class Bucket;

// Note: it is assumed that clients are well-behaved and only make one request
// for tokens per fill_interval, which is how it works with StreamRateLimiter.
//
// A client making requests more frequently could easily drain all available
// tokens and cause unfairness.
//
// This could be mitigated by having clients keep track of when they last
// requested tokens, and just return 0 without consulting the bucket if it
// hasn't been at least fill_interval, but a restriction for clients not behaving
// how the clients actually behaves seems unnecessary.
class Client : public TokenBucket {
public:
  Client(std::shared_ptr<Bucket> bucket, absl::string_view tenant_name, uint32_t tenant_weight)
      : tenant_name_(tenant_name), tenant_weight_(tenant_weight), bucket_(std::move(bucket)) {
    ASSERT(tenant_weight > 0);
  }
  ~Client() override;
  uint64_t consume(uint64_t tokens, bool allow_partial = true) override;
  uint64_t consume(uint64_t tokens, bool allow_partial,
                   std::chrono::milliseconds& time_to_next_token) override;
  std::chrono::milliseconds nextTokenAvailable() override;
  // Actual bucket belongs to the factory and is reset at create-time,
  // so maybeReset is a no-op.
  void maybeReset(uint64_t) override{};

private:
  const std::string tenant_name_;
  const uint32_t tenant_weight_;
  std::shared_ptr<Bucket> bucket_;
  bool known_limited_ = false;
  friend class Bucket;
};

// Tenant is hashable-as-string so it can be stored in a flat_hash_set and
// looked up by name.
struct Tenant {
  Tenant(absl::string_view name, uint64_t weight) : name_(name), weight_(weight) {}
  const std::string name_;
  const uint64_t weight_;
  // mutable because flat_hash_set getters are const due to assuming the entire
  // object is part of the hash-key, but here we explicitly hash only the name
  // and want to modify active_clients_ in place.
  mutable size_t active_clients_ = 1;
};
struct TenantHash {
  using is_transparent = void; // NOLINT(readability-identifier-naming)
  size_t operator()(const Tenant& t) const { return absl::Hash<std::string>{}(t.name_); }
  size_t operator()(absl::string_view str) const { return absl::Hash<absl::string_view>{}(str); }
  size_t operator()(const std::string& str) const { return absl::Hash<std::string>{}(str); }
};
struct TenantHashEq {
  using is_transparent = void; // NOLINT(readability-identifier-naming)
  bool operator()(const Tenant& lhs, const Tenant& rhs) const { return lhs.name_ == rhs.name_; }
  bool operator()(const Tenant& lhs, absl::string_view rhs_name) const {
    return lhs.name_ == rhs_name;
  }
  bool operator()(absl::string_view lhs_name, const Tenant& rhs) const {
    return lhs_name == rhs.name_;
  }
};

/**
 * A thread-safe wrapper class for TokenBucket interface which, when near
 * the limit, distributes tokens (weighted) among the set of tenants that
 * requested tokens.
 *
 * When not near the limit, tokens are given directly to clients without
 * keeping track of active tenants and clients, so that performance is
 * not impacted in the common path.
 *
 * "Near" here is defined as within one fill_interval worth of tokens
 * from the bucket being empty.
 */
class Bucket {
public:
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket (also the per-second
   * rate).
   * @param time_source supplies the time source.
   * @param fill_interval duration between token distributions when limiting.
   */
  static std::shared_ptr<Bucket>
  create(uint64_t max_tokens, TimeSource& time_source,
         std::chrono::milliseconds fill_interval = std::chrono::milliseconds{50});

  Bucket(const Bucket&) = delete;
  Bucket(Bucket&&) = delete;

  uint64_t requestTokens(Client& client, uint64_t want_tokens);
  void clientDestroyed(Client& client);

private:
  void clientDrained(Client& client) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void clientLimited(Client& client) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void purgeDrainedTenants() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  uint64_t tokensInBucket() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void consumeTokens(uint64_t) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  uint64_t tokensPerInterval() const;
  explicit Bucket(uint64_t max_tokens, TimeSource& time_source,
                  std::chrono::milliseconds fill_interval);
  Thread::MutexBasicLockable mutex_;
  TimeSource& time_source_;
  const std::chrono::milliseconds fill_interval_;
  const uint64_t max_tokens_;
  // The empty_at_ value is used to store how many tokens are in the bucket,
  // for example if empty_at_ is 100ms in the past, then there is 1/10 of
  // max_tokens_, or if 1000ms or more, then there is max_tokens_.
  MonotonicTime empty_at_ ABSL_GUARDED_BY(mutex_);
  absl::flat_hash_set<Tenant, TenantHash, TenantHashEq> active_tenants_ ABSL_GUARDED_BY(mutex_);
  std::vector<std::pair<MonotonicTime, std::string>> draining_tenants_ ABSL_GUARDED_BY(mutex_);
  uint64_t active_tenants_total_weight_ = 0;
};

} // namespace FairTokenBucket
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
