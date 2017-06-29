#pragma once

#include <condition_variable>
#include <deque>
#include <memory>
#include <utility>
#include <vector>

namespace Envoy {
namespace LogTransport {

/**
 * A Conveyor is a bidirectional thread safe queue.
 *
 * Like a conveyor in a mine the Conveyor is meant to transport "loaded"
 * objects in one direction and "empty" objects back in the other direction.
 * It is possible for there to be no "empty" objects available if processing
 * slows down. This makes for a natural backpressure feedback mechanism as data
 * is dropped instead of being logged.
 *
 * There are a few reasons for this approach:
 *   1) Reduced allocation activity. Most of the allocations will happen once, at
 *      startup.
 *   2) Natural upper bound on memory usage. Produced data will be dropped if
 *      log processing is unable to keep up rather than consuming unbounded amounts
 *      of memory.
 *   3) Fastest implementation on the insert-loaded side of the queue. There's
 *      never any allocation or object construction cost in the fast path, just
 *      pointer updates and accounting.
 *
 * If the producer would like to instead perform an allocation when there are no
 * "empty" objects available it can do the allocation and move-pass the unique_ptr to
 * the putLoaded() method.
 */
template <typename T> class Conveyor {
public:
  /**
   * Expand the number of objects used in the conveyor by performing
   * allocations.  Objects will be constructed using the remaining arguments
   * as constructor params (emplace style), default constructed if only the
   * count arugment is given.
   *
   * @param count The number of objects to allocate and add to the conveyor
   * @param args Constructor args (variadic).
   */
  template <typename... Args> void expand(int count, Args&&... args) {
    // We do the slower allocation outside of the lock
    std::vector<std::unique_ptr<T>> allocations(count);
    for (auto&& p : allocations) {
      p.reset(new T(std::forward<Args>(args)...));
    }

    // Then add the newly allocated items into the outbound deque under lock
    // It doesn't matter which end we add to.
    std::unique_lock<std::mutex> locked(lock_);
    for (auto&& p : allocations) {
      outbound_.push_back(std::move(p));
    }
  }

  /**
   * Contract the number of objects used in the conveyor to free memory.
   *
   * If there are not enough objects in the "unloaded" queue the entire unloaded
   * queue will be emptied.
   *
   * @param count The number of objects to try freeing.
   * @return int The number of objects actually freed.
   */
  int contract(int count) {
    int liberated = 0;
    while (liberated < count && !outbound_.empty()) {
      {
        std::unique_lock<std::mutex> locked(lock_);
        outbound_.pop_back(); // Implicitly deletes object as reference goes away
      }
      liberated++;
    }
    return liberated;
  }

  /**
   * Try to get an empty T object.
   *
   * If no empty objects are available this method will return nullptr.
   * It will not block.
   *
   * @return std::unique_ptr<T> Empty object, or nullptr.
   */
  std::unique_ptr<T> getEmpty() {
    std::unique_lock<std::mutex> locked(lock_);
    if (outbound_.empty()) {
      return std::unique_ptr<T>();
    } else {
      auto obj = std::move(outbound_.back());
      outbound_.pop_back();
      return obj;
    }
  }

  /**
   * Place a loaded object onto the Conveyor for processing.
   *
   * @param obj An std::unique_ptr<T> to a loaded object ready for processing
   */
  void putLoaded(std::unique_ptr<T>&& obj) {
    {
      std::unique_lock<std::mutex> locked(lock_);
      inbound_.push_front(std::move(obj));
    }
    queue_event_.notify_one();
  }

  /**
   * Block and wait for one loaded object.
   *
   * @return std::unique_ptr<T> A loaded object ready for processing.
   */
  std::unique_ptr<T> waitAndGetLoaded() {
    std::unique_lock<std::mutex> locked(lock_);
    queue_event_.wait(locked, [this]() -> bool { return !inbound_.empty(); });
    auto obj = std::move(inbound_.back());
    inbound_.pop_back();
    return obj;
  }

  /**
   * Return a processed ("empty") object.
   *
   * @param obj An std::unique_ptr<T> to an empty object ready for reuse
   */
  void putEmpty(std::unique_ptr<T>&& obj) {
    std::unique_lock<std::mutex> locked(lock_);
    outbound_.push_front(std::move(obj));
  }

private:
  // Mutex protects access to queues and condition variable
  std::mutex lock_;
  // Condition variable signals that loaded objects are available
  std::condition_variable_any queue_event_;
  // "Inbound" is defined as the direction towards the Conveyor object
  std::deque<std::unique_ptr<T>> inbound_;
  // "Outbound" is defined as the direction back towards inserters
  std::deque<std::unique_ptr<T>> outbound_;
};

} // LogTransport
} // Envoy
