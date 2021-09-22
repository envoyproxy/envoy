#pragma once

#include <functional>
#include <memory>

namespace Envoy {
namespace Upstream {

/**
 * The base class for scheduler implementations used in various load balancers.
 */
template <class C> class Scheduler {
public:
  virtual ~Scheduler() = default;

  /**
   * Each time peekAgain is called, it will return the best-effort subsequent
   * pick, popping and reinserting the entry as if it had been picked.
   * The first time peekAgain is called, it will return the
   * first item which will be picked, the second time it is called it will
   * return the second item which will be picked. As picks occur, that window
   * will shrink.
   *
   * @param calculate_weight for implementations that choose to support it, this predicate specifies
   * the new weight of the entry.
   * @return std::shared_ptr<C> the best effort subsequent pick.
   */

  virtual std::shared_ptr<C> peekAgain(std::function<double(const C&)> calculate_weight) = 0;

  /**
   * Pick a queue entry with closest deadline.
   *
   * @param calculate_weight for implementations that choose to support it, this predicate specifies
   * the new weight of the entry.
   * @return std::shared_ptr<C> to next valid the queue entry if or nullptr if none exists.
   */
  virtual std::shared_ptr<C> pickAndAdd(std::function<double(const C&)> calculate_weight) = 0;

  /**
   * Insert entry into queue with a given weight.
   *
   * @param weight entry weight.
   * @param entry shared pointer to entry.
   */
  virtual void add(double weight, std::shared_ptr<C> entry) = 0;

  /**
   * Returns true if the scheduler is empty and nothing has been added.
   *
   * @return bool whether or not the internal container is empty.
   */
  virtual bool empty() const = 0;
};

} // namespace Upstream
} // namespace Envoy
