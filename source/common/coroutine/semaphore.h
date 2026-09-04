#pragma once

#include <cstdint>
#include <limits>
#include <list>
#include <memory>
#include <optional>
#include <utility>

#include "source/common/common/assert.h"
#include "source/common/coroutine/launch.h"
#include "source/common/coroutine/leaf_awaitable.h"
#include "source/common/coroutine/task.h"

#include "absl/functional/any_invocable.h"
#include "absl/status/status.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Coroutine {

class Semaphore;

/**
 * SemaphoreReservation is an RAII guard holding a reservation against a Semaphore.
 * When destroyed or explicitly released, it automatically decrements the reserved permits
 * in Semaphore and triggers waiter processing.
 */
class SemaphoreReservation {
public:
  SemaphoreReservation() = default;
  SemaphoreReservation(std::weak_ptr<Semaphore> sem, uint64_t permits);
  ~SemaphoreReservation();

  SemaphoreReservation(const SemaphoreReservation&) = delete;
  SemaphoreReservation& operator=(const SemaphoreReservation&) = delete;

  SemaphoreReservation(SemaphoreReservation&& other) noexcept;
  SemaphoreReservation& operator=(SemaphoreReservation&& other) noexcept;

  uint64_t permits() const { return permits_; }
  bool hasPermits() const { return !sem_.expired() && permits_ > 0; }
  void release();

private:
  std::weak_ptr<Semaphore> sem_;
  uint64_t permits_{0};
};

/**
 * Semaphore is an asynchronous FIFO weighted semaphore for Envoy coroutines.
 *
 * It preserves strict FIFO arrival ordering for permit acquisition. Waiters that cannot be
 * immediately satisfied are queued. When permits are released, processing resumes pending
 * waiters in strict FIFO order in the next event loop iteration.
 *
 * Anti-starvation for oversized requests:
 * When the semaphore is completely idle (`currentPermits() == 0`), a single oversized
 * acquisition (`permits > maxPermits()`) is allowed to acquire and proceed. This ensures that
 * items larger than the nominal capacity limit are not permanently starved or deadlocked.
 * While an oversized reservation is active, subsequent acquisitions are blocked until it is
 * released.
 */
class Semaphore : public std::enable_shared_from_this<Semaphore> {
public:
  struct Waiter {
    uint64_t permits{0};
    absl::AnyInvocable<void(absl::StatusOr<SemaphoreReservation>)> cb;
    std::shared_ptr<Executor> executor;
  };

  class SemaphoreAwaitable : public LeafAwaitable<absl::StatusOr<SemaphoreReservation>> {
  public:
    SemaphoreAwaitable(Semaphore& sem, uint64_t permits);

  protected:
    std::optional<absl::StatusOr<SemaphoreReservation>> tryImmediate() override;
    void onStart() override;
    void onCancel() override;

  private:
    Semaphore& sem_;
    uint64_t permits_;
    std::shared_ptr<Waiter> waiter_;
  };

  explicit Semaphore(std::optional<uint64_t> max_permits = std::nullopt);
  ~Semaphore();

  Semaphore(const Semaphore&) = delete;
  Semaphore& operator=(const Semaphore&) = delete;
  Semaphore(Semaphore&&) = delete;
  Semaphore& operator=(Semaphore&&) = delete;

  std::optional<uint64_t> maxPermits() const { return max_permits_; }
  uint64_t currentPermits() const { return current_permits_; }

  /**
   * Attempts to synchronously acquire `permits` without suspending.
   * If the semaphore has sufficient capacity (or is completely idle for a single oversized
   * acquisition), returns a SemaphoreReservation. Otherwise returns std::nullopt.
   */
  std::optional<SemaphoreReservation> tryAcquire(uint64_t permits = 1);

  /**
   * Asynchronously acquires `permits` following strict FIFO ordering.
   * If capacity is not immediately available, suspends the coroutine until sufficient permits
   * are released. Supports a single oversized acquisition when the semaphore is fully drained.
   */
  SemaphoreAwaitable acquire(uint64_t permits = 1);

private:
  friend class SemaphoreReservation;
  friend class SemaphoreAwaitable;

  void release(uint64_t permits);
  bool hasPermits(uint64_t additional_permits) const;
  bool canAcquire(uint64_t permits) const;
  void popCancelledWaiters();
  void processWaiters();
  void scheduleProcessWaiters();
  static Task<absl::Status> runScheduledProcessWaiters(std::weak_ptr<Semaphore> weak_self,
                                                       std::shared_ptr<bool> alive);

  const std::optional<uint64_t> max_permits_;
  uint64_t current_permits_{0};
  std::list<std::shared_ptr<Waiter>> waiters_;
  std::optional<DetachedHandle> process_handle_;
  std::shared_ptr<bool> alive_{std::make_shared<bool>(true)};
};

using SemaphorePtr = std::shared_ptr<Semaphore>;

} // namespace Coroutine
} // namespace Envoy
