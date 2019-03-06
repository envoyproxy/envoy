#pragma once

#include <list>

#include "common/callback/callback.h"

namespace Envoy {
namespace Common {
namespace Callback {

/**
 * Callback::Manager is a fairly typical implementation of the "Observer" pattern
 * (https://en.wikipedia.org/wiki/Observer_pattern), made safe by using Callback::Caller. Any number
 * of Callers may be added to a Manager. Resetting or destroying a Caller's corresponding Receiver
 * will remove it from the Manager's list when it is invoked.
 *
 * Manager is actually a type alias (below) to this class template, ManagerT, which is parameterized
 * on an additional type C, representing the caller type. This will typically be Caller<Args...>,
 * but can be anything that behaves like a function. See examples in manager_test.cc (where the
 * caller type is a mock), and Init::ManagerImpl (where the caller does some extra logging).
 */
template <typename C, typename... Args> class ManagerT {
public:
  /**
   * Adds a Caller to the callback manager, such that its corresponding Receiver will be invoked
   * by subsequent calls to call() as long as it remains available.
   * @param caller the caller to add to the callback manager
   */
  ManagerT& add(C caller) {
    callers_.push_back(std::move(caller));
    return *this;
  }

  /**
   * Invokes all callers previously provided to add(). Any corresponding receivers that are no
   * longer available after invocation (which may have happened as a side-effect of invoking them)
   * will be removed from the callback manager.
   * @param args the arguments, if any, to pass to all callers.
   */
  void operator()(Args... args) {
    for (auto it = callers_.begin(); it != callers_.end(); /* incremented below */) {
      // First, invoke the caller whether or not it references an available receiver.
      const auto& caller = *it;
      caller(args...);

      // The caller may reference an unavailable receiver, either because the receiver was already
      // unavailable beforehand, or because it was reset or destroyed as a side-effect of invoking
      // it. If the receiver is unavailable, remove it so we don't try to call it again.
      if (caller) {
        ++it;
      } else {
        it = callers_.erase(it);
      }
    }
  }

private:
  std::list<C> callers_;
};

template <typename... Args> using Manager = ManagerT<Caller<Args...>, Args...>;

} // namespace Callback
} // namespace Common
} // namespace Envoy
