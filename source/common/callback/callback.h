#pragma once

#include <functional>
#include <memory>

namespace Envoy {
namespace Common {

/**
 * The Callback::Caller and Callback::Receiver classes address a memory safety issue with callbacks
 * in C++. Typically, an "event consumer" (a.k.a. handler, listener, observer) might register
 * interest with an "event producer" (a.k.a. manager, subject) either by implementing an OO-style
 * callback interface like:
 *
 *   struct EventConsumer {
 *     virtual void onEvent(const Event& e) = 0;
 *   };
 *
 *   class MyEventConsumer : public EventConsumer {
 *   public:
 *     MyEventConsumer(EventProducer& producer) { producer.attach(this); }
 *     void onEvent(const Event& e) override { ... handle event ... }
 *   };
 *
 *   class EventProducer {
 *   public:
 *     void attach(EventConsumer* consumer) { consumer_ = consumer; }
 *     void invoke() { consumer_.onEvent(... some event ...); }
 *   private:
 *     EventConsumer* consumer_;
 *   };
 *
 * ... or by passing a functional-style callback like:
 *
 *   class MyEventConsumer {
 *   public:
 *     MyEventConsumer(EventProducer& producer) {
 *       producer.attach([this]() { ... handle event ... });
 *     }
 *   };
 *
 *   class EventProducer {
 *   public:
 *     void attach(std::function<void(const Event&)> callback) { callback_ = callback; }
 *     void invoke() { callback_(... some event ...); }
 *   private:
 *     std::function<void(const Event&)> callback_;
 *   };
 *
 *
 * These approaches are equivalent, and they both have the same issue: the event producer
 * references the event consumer, either directly or via the lambda's captures, but doesn't manage
 * its lifetime. If the event consumer is destroyed before the event producer calls it, this is a
 * use-after-free.
 *
 * In some cases, it's straightforward enough to implement "cancelation" by allowing an event
 * consumer to be detached from any event producers it is currently attached to, but that requires
 * holding references to all such event producers. That may be impractical or, again, unsafe in the
 * case where the event consumer outlives its producers.
 *
 * Caller and Receiver provide some additional safety. A Receiver owns a callback function, and
 * can produce Callers which function as weak references to it. The receiver's callback function
 * can only be invoked via its callers. If the receiver is destroyed, invoking its callers has
 * no effect, so none of the callback's captures can be unsafely dereferenced.
 *
 * When implementing this pattern, an event consumer would own a Receiver and an event producer
 * would own the corresponding Caller. For example:
 *
 *   using EventCaller = Callback::Caller<const Event&>;
 *   using EventReceiver = Callback::Receiver<const Event&>;
 *
 *   class MyEventConsumer {
 *   public:
 *     MyEventConsumer(EventProducer& producer) {
 *       producer.attach(receiver_.caller());
 *     }
 *   private:
 *     EventReceiver receiver_([this]() { ... handle event ... });
 *   };
 *
 *   class EventProducer {
 *   public:
 *     void attach(EventCaller caller) { caller_ = caller; }
 *     void invoke() { caller_(... some event ... ); }
 *   private:
 *     EventCaller caller_;
 *   };
 */
namespace Callback {

// Forward-declaration for Caller's friend declaration.
template <typename... Args> class Receiver;

/**
 * Caller: simple wrapper for a weak_ptr to a callback function. Copyable and movable.
 */
template <typename... Args> class Caller {
public:
  /**
   * Default constructor for default / value initialization.
   */
  Caller() = default;

  /**
   * Implicit conversion to bool, to test whether the corresponding Receiver is still available.
   * @return true if the corresponding Receiver is still available, false otherwise.
   */
  operator bool() const { return !fn_.expired(); }

  /**
   * Reset this caller to not reference a Receiver.
   */
  void reset() { fn_.reset(); }

  /**
   * Invoke the corresponding Receiver's callback, if it is still available. If the receiver has
   * been destroyed or reset, this has no effect.
   * @param args the arguments, if any, to pass to the receiver's callback function.
   */
  void operator()(Args... args) const {
    auto locked_fn(fn_.lock());
    if (locked_fn) {
      (*locked_fn)(args...);
    }
  }

private:
  /**
   * Can only be constructed by a Receiver
   */
  friend Receiver<Args...>;
  Caller(std::weak_ptr<std::function<void(Args...)>> fn) : fn_(std::move(fn)) {}

  std::weak_ptr<std::function<void(Args...)>> fn_;
};

/**
 * Receiver: simple wrapper for a shared_ptr to a callback function. Copyable and movable, but
 * typically should be owned uniquely by the owner of any pointers and references captured by its
 * handler function. For example, if `this` is captured by the handler function, `this` should
 * probably also own the Receiver.
 */
template <typename... Args> class Receiver {
public:
  /**
   * Default constructor for default / value initialization.
   */
  Receiver() = default;

  /**
   * Construct a receiver to own a callback function.
   */
  Receiver(std::function<void(Args...)> fn)
      : fn_(std::make_shared<std::function<void(Args...)>>(std::move(fn))) {}

  /**
   * @return a new caller for this receiver.
   */
  Caller<Args...> caller() const {
    return Caller<Args...>(std::weak_ptr<std::function<void(Args...)>>(fn_));
  }

  /**
   * Reset this receiver, such that any callers previously created will not be able to invoke it.
   */
  void reset() { fn_.reset(); }

  /**
   * Explicit conversion to bool, to test whether the receiver contains a callback function.
   * @return true if the corresponding Receiver contains a callback, false otherwise.
   */
  explicit operator bool() const { return static_cast<bool>(fn_); }

private:
  std::shared_ptr<std::function<void(Args...)>> fn_;
};

} // namespace Callback
} // namespace Common
} // namespace Envoy
