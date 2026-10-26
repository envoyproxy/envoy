#include "source/common/coroutine/semaphore.h"

namespace Envoy {
namespace Coroutine {

SemaphoreReservation::SemaphoreReservation(std::weak_ptr<Semaphore> sem, uint64_t permits)
    : sem_(std::move(sem)), permits_(permits) {}

SemaphoreReservation::~SemaphoreReservation() { release(); }

SemaphoreReservation::SemaphoreReservation(SemaphoreReservation&& other) noexcept
    : sem_(std::move(other.sem_)), permits_(std::exchange(other.permits_, 0)) {}

SemaphoreReservation& SemaphoreReservation::operator=(SemaphoreReservation&& other) noexcept {
  if (this != &other) {
    if (permits_ > 0) {
      IS_ENVOY_BUG("SemaphoreReservation should not overwrite an active reservation");
      release();
    }
    sem_ = std::move(other.sem_);
    permits_ = std::exchange(other.permits_, 0);
  }
  return *this;
}

void SemaphoreReservation::release() {
  const uint64_t permits = std::exchange(permits_, 0);
  std::shared_ptr<Semaphore> sem = sem_.lock();
  sem_.reset();
  if (permits > 0 && sem != nullptr) {
    sem->release(permits);
  }
}

Semaphore::SemaphoreAwaitable::SemaphoreAwaitable(Semaphore& sem, uint64_t permits)
    : sem_(sem), permits_(permits) {}

std::optional<absl::StatusOr<SemaphoreReservation>> Semaphore::SemaphoreAwaitable::tryImmediate() {
  std::optional<SemaphoreReservation> res = sem_.tryAcquire(permits_);
  if (res.has_value()) {
    return res;
  }
  return std::nullopt;
}

void Semaphore::SemaphoreAwaitable::onStart() {
  waiter_ = std::make_shared<Semaphore::Waiter>();
  waiter_->permits = permits_;
  waiter_->executor = this->context().executorShared();
  waiter_->cb = [this](absl::StatusOr<SemaphoreReservation> res) {
    this->complete(std::move(res));
  };
  sem_.waiters_.push_back(waiter_);
}

void Semaphore::SemaphoreAwaitable::onCancel() {
  ASSERT(waiter_ != nullptr);
  waiter_->cb = nullptr;
  sem_.scheduleProcessWaiters();
}

Semaphore::Semaphore(std::optional<uint64_t> max_permits) : max_permits_(max_permits) {
  if (max_permits_.has_value()) {
    ASSERT(*max_permits_ > 0, "max_permits must be positive if specified");
  }
}

Semaphore::~Semaphore() {
  *alive_ = false;
  if (process_handle_.has_value()) {
    // Note: cancel() is a no-op here because runScheduledProcessWaiters does not await any
    // leaf awaitables and executes synchronously once scheduled. Setting *alive_ = false
    // above guarantees the task will observe destruction and return immediately.
    process_handle_->cancel();
    // Resetting process_handle_ drops the DetachedHandle. The underlying RootTask frame
    // becomes self-owned and will automatically clean itself up once it runs on the executor.
    process_handle_.reset();
  }
  std::list<std::shared_ptr<Waiter>> waiters;
  waiters.swap(waiters_);
  for (std::shared_ptr<Waiter>& w : waiters) {
    if (w->cb) {
      absl::AnyInvocable<void(absl::StatusOr<SemaphoreReservation>)> cb = std::move(w->cb);
      cb(absl::FailedPreconditionError("Semaphore is destroyed"));
    }
  }
}

bool Semaphore::hasPermits(uint64_t additional_permits) const {
  ASSERT(max_permits_.has_value());
  if (current_permits_ > *max_permits_) {
    return false;
  }
  if (additional_permits > std::numeric_limits<uint64_t>::max() - current_permits_) {
    return false;
  }
  return (current_permits_ + additional_permits) <= *max_permits_;
}

bool Semaphore::canAcquire(uint64_t permits) const {
  if (!max_permits_.has_value()) {
    return true;
  }
  if (current_permits_ == 0 && permits > *max_permits_) {
    return true;
  }
  return hasPermits(permits);
}

void Semaphore::popCancelledWaiters() {
  while (!waiters_.empty() && !waiters_.front()->cb) {
    waiters_.pop_front();
  }
}

std::optional<SemaphoreReservation> Semaphore::tryAcquire(uint64_t permits) {
  popCancelledWaiters();
  if (waiters_.empty() && canAcquire(permits)) {
    current_permits_ += permits;
    return SemaphoreReservation(shared_from_this(), permits);
  }
  return std::nullopt;
}

Semaphore::SemaphoreAwaitable Semaphore::acquire(uint64_t permits) {
  return SemaphoreAwaitable(*this, permits);
}

void Semaphore::release(uint64_t permits) {
  ASSERT(permits > 0);
  ASSERT(current_permits_ >= permits);
  current_permits_ -= permits;
  scheduleProcessWaiters();
}

Task<absl::Status> Semaphore::runScheduledProcessWaiters(std::weak_ptr<Semaphore> weak_self,
                                                         std::shared_ptr<bool> alive) {
  if (!*alive) {
    co_return absl::OkStatus();
  }
  std::shared_ptr<Semaphore> sem = weak_self.lock();
  ASSERT(sem != nullptr);
  sem->process_handle_.reset();
  sem->processWaiters();
  co_return absl::OkStatus();
}

void Semaphore::scheduleProcessWaiters() {
  if (process_handle_.has_value() || waiters_.empty()) {
    return;
  }

  popCancelledWaiters();
  if (waiters_.empty()) {
    return;
  }

  if (!canAcquire(waiters_.front()->permits)) {
    return;
  }

  std::shared_ptr<Executor> exec;
  for (const std::shared_ptr<Waiter>& w : waiters_) {
    if (w->cb && w->executor != nullptr) {
      exec = w->executor;
      break;
    }
  }
  ASSERT(exec != nullptr);

  process_handle_ = launch(
      runScheduledProcessWaiters(weak_from_this(), alive_), std::move(exec), [](absl::Status) {},
      StartMode::Scheduled);
}

void Semaphore::processWaiters() {
  std::shared_ptr<bool> alive = alive_;
  while (*alive && !waiters_.empty()) {
    popCancelledWaiters();
    if (waiters_.empty()) {
      break;
    }

    std::shared_ptr<Waiter> head = waiters_.front();
    if (!canAcquire(head->permits)) {
      break;
    }

    waiters_.pop_front();
    current_permits_ += head->permits;
    SemaphoreReservation reservation(shared_from_this(), head->permits);

    absl::AnyInvocable<void(absl::StatusOr<SemaphoreReservation>)> cb = std::move(head->cb);
    if (cb) {
      cb(std::move(reservation));
    }
  }
}

} // namespace Coroutine
} // namespace Envoy
