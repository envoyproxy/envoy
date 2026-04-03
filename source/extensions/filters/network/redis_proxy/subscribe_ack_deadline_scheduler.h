#pragma once

#include <chrono>
#include <cstdint>
#include <deque>
#include <functional>
#include <string>
#include <utility>

#include "envoy/common/time.h"
#include "envoy/event/dispatcher.h"
#include "envoy/event/timer.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace RedisProxy {

/**
 * A single shared timeout timer over many per-channel subscribe-ack deadlines (E4). Every pending
 * SUBSCRIBE that awaits its upstream ack registers a deadline here; because the timeout is
 * constant, deadlines are pushed in monotonically non-decreasing order, so ONE timer armed for the
 * earliest outstanding deadline suffices — replacing the former one-timer-per-channel scheme (a
 * 1000-channel subscribe armed 1000 libevent timers).
 *
 * Extracted from SubscriptionRegistry (D3) as an independently unit-testable component. It owns
 * only the deadline queue + timer; the registry supplies two predicates so the scheduler carries no
 * bucket state:
 *   - ``is_live(channel, seq)``: is the entry's pending bucket still the one that registered this
 *     token? A stale entry (bucket acked/dropped, or re-created under a newer token) is skipped and
 *     pruned lazily rather than on bucket erase, keeping bucket teardown O(1).
 *   - ``on_expired(channel)``: the channel's deadline elapsed with no ack — fail + roll it back.
 */
class SubscribeAckDeadlineScheduler {
public:
  using LivePredicate = std::function<bool(const std::string& channel, uint64_t seq)>;
  using ExpiredCallback = std::function<void(const std::string& channel)>;

  SubscribeAckDeadlineScheduler(Event::Dispatcher& dispatcher, std::chrono::milliseconds timeout,
                                LivePredicate is_live, ExpiredCallback on_expired)
      : dispatcher_(dispatcher), timeout_(timeout), is_live_(std::move(is_live)),
        on_expired_(std::move(on_expired)) {}

  /**
   * Register a deadline for ``channel`` and return the token the caller MUST store on its bucket
   * (the ``is_live`` predicate matches against it on fire). Arms the shared timer only on the empty
   * -> non-empty edge; when the queue was already non-empty the timer is armed for an earlier
   * (front) deadline and this later entry is picked up when the front rolls off. The timer is
   * created lazily on the first registration, so a scheduler that is never used allocates none.
   */
  uint64_t schedule(const std::string& channel) {
    const uint64_t seq = next_seq_++;
    const MonotonicTime deadline = dispatcher_.timeSource().monotonicTime() + timeout_;
    const bool was_empty = schedule_.empty();
    schedule_.push_back({deadline, seq, channel});
    if (was_empty) {
      if (timer_ == nullptr) {
        timer_ = dispatcher_.createTimer([this]() { onTimeout(); });
      }
      timer_->enableTimer(timeout_);
    }
    return seq;
  }

  /**
   * Drop now-dead leading entries and re-arm for the earliest LIVE deadline (or disable the timer
   * if none remain). Call after an ack delivery drains a bucket (eager prune — G14) so the timer
   * never wakes to a token-miss no-op.
   */
  void pruneAndRearm() {
    if (timer_ == nullptr) {
      return; // nothing was ever scheduled, so there is no timer to prune / re-arm
    }
    dropDeadLeading();
    if (schedule_.empty()) {
      timer_->disableTimer();
      return;
    }
    const MonotonicTime now = dispatcher_.timeSource().monotonicTime();
    const MonotonicTime next = schedule_.front().deadline;
    const auto delay = next > now
                           ? std::chrono::duration_cast<std::chrono::milliseconds>(next - now)
                           : std::chrono::milliseconds(0);
    timer_->enableTimer(delay);
  }

  /**
   * Drop the whole queue and disable the timer (the registry's clear() on cluster removal /
   * update).
   */
  void clear() {
    schedule_.clear();
    if (timer_ != nullptr) {
      timer_->disableTimer();
    }
  }

private:
  struct Deadline {
    MonotonicTime deadline;
    uint64_t seq;
    std::string channel;
  };

  void dropDeadLeading() {
    while (!schedule_.empty()) {
      const Deadline& front = schedule_.front();
      if (is_live_(front.channel, front.seq)) {
        break; // front is a live pending bucket
      }
      schedule_.pop_front();
    }
  }

  void onTimeout() {
    if (schedule_.empty()) {
      return;
    }
    // Drain every entry already DUE in this ONE callback, up to the later of ``now`` and the front
    // deadline. A delayed dispatcher can leave several DISTINCT deadlines overdue at once; handling
    // only the front deadline's group and re-arming a 0ms timer for the next would churn one
    // wake-up per overdue deadline. In production the timer fires at/after the front deadline, so
    // the cutoff is ``now`` and every overdue entry expires here; a manually-fired test timer that
    // has NOT advanced the wall clock still drains at least the front deadline's group (now < front
    // deadline -> cutoff is the front deadline). Draining by a deadline cutoff (not per-entry
    // "now") keeps the batch-SUBSCRIBE case — many channels registered at one instant sharing one
    // deadline — expiring together, and stays correct under the test's frozen clock.
    const MonotonicTime now = dispatcher_.timeSource().monotonicTime();
    const MonotonicTime front_deadline = schedule_.front().deadline;
    const MonotonicTime cutoff = now > front_deadline ? now : front_deadline;
    while (!schedule_.empty() && schedule_.front().deadline <= cutoff) {
      const Deadline entry =
          schedule_.front(); // copy: on_expired may erase the bucket / re-register
      schedule_.pop_front();
      if (is_live_(entry.channel, entry.seq)) {
        on_expired_(entry.channel);
      }
    }
    pruneAndRearm();
  }

  Event::Dispatcher& dispatcher_;
  const std::chrono::milliseconds timeout_;
  const LivePredicate is_live_;
  const ExpiredCallback on_expired_;
  std::deque<Deadline> schedule_;
  Event::TimerPtr timer_;
  uint64_t next_seq_{0};
};

} // namespace RedisProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
