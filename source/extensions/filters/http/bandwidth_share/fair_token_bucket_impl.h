#pragma once

#include <chrono>
#include <memory>
#include <string>
#include <utility>
#include <vector>

#include "source/common/common/thread.h"
#include "source/common/common/thread_synchronizer.h"
#include "source/common/common/token_bucket_impl.h"

#include "absl/container/flat_hash_map.h"
#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace BandwidthShareFilter {
namespace FairTokenBucket {

/**
 * The structure here is a cascade of buckets. The top bucket features a TokenBucketImpl
 * that it draws from. In the common non-limited case, a request simply fetches tokens
 * from the TokenBucketImpl directly, and returns them, because limits have not been
 * reached.
 *
 * Once limits have been reached, the secondary behavior kicks in - buckets spilling
 * tokens evenly into their children as specified by their weights (up to the total
 * amount requested by that child).
 *
 * Spilling happens on a periodic basis to avoid performing the relatively expensive
 * action at the unlimited frequency that it would be if it was triggered by every
 * request.
 *
 *                Factory (TokenBucketImpl)
 *                   |
 *        +------+---+------------+
 *        |      |                |
 *    Tenant   Tenant           Tenant
 *     |        |                 |
 *  Request   Request       +-----+---+------+
 *                          |         |      |
 *                     Request    Request   Request
 *
 * The interface for actual use is `Client` which wraps a Request and provides
 * [the relevant part of] the TokenBucket interface. This wrapper allows
 * RAII to cancel requests for tokens and perform appropriate structural
 * deletion while the relevant locks are held, dodging the pitfalls of
 * std::weak_ptrs being invalidated before destructors run, normally.
 */

class Request;
class Tenant;

/**
 * A thread-safe wrapper class for TokenBucket interface which, when blocking,
 * distributes tokens weighted-evenly among the set of tenants that requested tokens.
 */
class Factory : public std::enable_shared_from_this<Factory> {
public:
  static const char ConsumeSyncPoint[];
  /**
   * @param max_tokens supplies the maximum number of tokens in the bucket.
   * @param time_source supplies the time source.
   * @param spill_frequency duration between token distributions when limiting.
   * @param fill_rate supplies the number of tokens that will return to the bucket per second.
   * The default is max_tokens.
   */
  static std::shared_ptr<Factory>
  create(uint64_t max_tokens, TimeSource& time_source,
         std::chrono::milliseconds spill_frequency = std::chrono::milliseconds{50},
         double fill_rate = 0);

  Factory(const Factory&) = delete;
  Factory(Factory&&) = delete;

  std::shared_ptr<Tenant> getTenant(absl::string_view tenant_name, uint64_t weight = 1);

private:
  explicit Factory(uint64_t max_tokens, TimeSource& time_source,
                   std::chrono::milliseconds spill_frequency, double fill_rate);
  void spill() ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void addToQueue(std::shared_ptr<Tenant> tenant) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  void removeFromQueue(std::shared_ptr<Tenant> tenant) ABSL_EXCLUSIVE_LOCKS_REQUIRED(mutex_);
  Thread::MutexBasicLockable mutex_;
  TokenBucketImpl impl_ ABSL_GUARDED_BY(mutex_);
  TimeSource& time_source_;
  absl::optional<MonotonicTime> next_spill_ ABSL_GUARDED_BY(mutex_);
  std::chrono::milliseconds spill_frequency_;
  bool is_limiting_ ABSL_GUARDED_BY(mutex_){false};
  std::vector<std::shared_ptr<Tenant>> waiting_tenants_ ABSL_GUARDED_BY(mutex_);
  Thread::MutexBasicLockable tenants_mutex_;
  // Map from tenant_name to Tenant.
  // Key is the name in the tenant object so we can use string_view,
  // as the entry is removed from the map during Tenant's destructor,
  // i.e. it is scoped with the same lifetime.
  absl::flat_hash_map<absl::string_view, std::weak_ptr<Tenant>>
      active_tenants_ ABSL_GUARDED_BY(tenants_mutex_);
  mutable Thread::ThreadSynchronizer synchronizer_; // Used only for testing.
  friend class FactoryTest;
  friend class Request;
  friend class Tenant;
  friend class SpillHandler;
};

class Tenant : public std::enable_shared_from_this<Tenant> {
public:
  explicit Tenant(absl::string_view name, std::shared_ptr<Factory> parent, uint64_t weight)
      : name_(name), parent_(std::move(parent)), weight_(weight) {}
  Tenant(const Tenant&) = delete;
  Tenant(Tenant&&) = delete;

  std::shared_ptr<Request> makeRequest();
  void addToQueue(std::shared_ptr<Request> request) ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_);
  void removeFromQueue(std::shared_ptr<Request> request)
      ABSL_EXCLUSIVE_LOCKS_REQUIRED(&Factory::mutex_);
  ~Tenant();

private:
  const std::string name_;
  std::shared_ptr<Factory> parent_;
  // Non-const weight allows for configuration changes to dynamically modify flow.
  uint64_t weight_ ABSL_GUARDED_BY(&Factory::mutex_){1};
  uint64_t want_tokens_ ABSL_GUARDED_BY(&Factory::mutex_){0};
  std::vector<std::shared_ptr<Request>> waiting_requests_ ABSL_GUARDED_BY(&Factory::mutex_);
  friend class Factory;
  friend class Request;
  friend class SpillHandler;
};

class Request : public std::enable_shared_from_this<Request> {
public:
  Request(const Request&) = delete;
  Request(Request&&) = delete;

private:
  uint64_t consume(uint64_t tokens);
  void cancel();
  explicit Request(std::shared_ptr<Tenant> tenant) : tenant_(std::move(tenant)) {}
  uint64_t held_tokens_ ABSL_GUARDED_BY(&Factory::mutex_){0};
  uint64_t queued_tokens_ ABSL_GUARDED_BY(&Factory::mutex_){0};
  std::shared_ptr<Tenant> tenant_;
  friend class Client;
  friend class Tenant;
  friend class SpillHandler;
};

class Client : public TokenBucket {
public:
  Client(Factory& factory, absl::string_view tenant_name, uint64_t weight);
  Client(const Client&) = delete;
  Client(Client&&) = delete;
  ~Client() override;
  uint64_t consume(uint64_t tokens, bool allow_partial = true) override;
  uint64_t consume(uint64_t tokens, bool allow_partial,
                   std::chrono::milliseconds& time_to_next_token) override;
  std::chrono::milliseconds nextTokenAvailable() override;
  // Actual bucket belongs to the factory and is reset at create-time,
  // so maybeReset is a no-op.
  void maybeReset(uint64_t) override{};

private:
  std::shared_ptr<Request> request_;
};

} // namespace FairTokenBucket
} // namespace BandwidthShareFilter
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
