#pragma once

#include <vector>

#include "common/common/assert.h"

//#include "envoy/access_log/access_log.h"

namespace Envoy {
namespace LogTransport {

/**
 * Provide a generic wrapper for a block of multiple log objects (of any type).
 *
 * Includes the ability to clear the block back to a newly constructed state
 * (without redoing allocations). This enables passing LogObjectStorage objects
 * through a queue to a log processing thread which passes them back cleared out
 * when it is done processing.
 */
template <typename T> class LogObjectStore {
public:
  /**
   * Construct a new store with certain capacity.
   *
   * @param capacity The number of items that can be stored
   */
  LogObjectStore(const int capacity) : use_count_(0), store_(capacity) {}

  /**
   * Check if the LogObjectStore is full.
   *
   * @return True if the store is full, false if there is still room.
   */
  bool isFull() const {
    ASSERT(use_count_ <= store_.size());
    return use_count_ == store_.size();
  }

  /**
   * Return the number of active entries in the LogObjectStore.
   *
   * This should be equal to the number of items found when iterating over the
   * container using begin() -> end() method.
   *
   * @return int Number of entries in the LogObjectStore.
   */
  int size() const { return use_count_; }

  /**
   * Reset the object back to newly constructed state.
   *
   * The stored data itself is not altered.
   */
  void clear() { use_count_ = 0; }

  /**
   * Reset the object back to newly constructed state and run clearing
   * function on all stored objects.
   *
   * For example if protos are stored:
   *   clear([](auto& proto) { proto.Clear(); });
   *
   * @param f A function run on all items in the store.
   */
  template <typename Func> void clear(Func f) {
    clear();
    for (auto& item : store_) {
      f(item);
    }
  }

  /**
   * Get a mutable reference to the next writable entry.
   *
   * Ownership is not passed to the caller.
   *
   * You must first check that the LogObjectStore is not full with the isFull()
   * method.  To do otherwise is a programming error.
   *
   * @return T& A reference to object of the template type.
   */
  T& getNextWritableEntry() {
    ASSERT(use_count_ < store_.size());
    return store_[use_count_++];
  }

  // To enable using a range-based for loops on this object we mostly
  // leverage the std::vector iterator implementation.
  //
  // The end iterator is the vector begin() + use_count_, so if partially
  // full iteration will stop at the correct place.
  //
  // If use_count_ is 0 begin() will return the same thing as end() giving the
  // expected result.
  //
  // Only const iteration is implemented. If non-const iteration is required
  // in the future just add the typedef and non-const methods below.
  typedef typename std::vector<T>::const_iterator const_iterator;

  const_iterator begin() const { return store_.begin(); }
  const_iterator cbegin() const { return store_.begin(); }

  const_iterator end() const { return store_.begin() + use_count_; }
  const_iterator cend() const { return store_.begin() + use_count_; }

private:
  unsigned int use_count_;
  std::vector<T> store_;
};

} // LogTransport
} // Envoy
