#pragma once

#include <memory>

#include "envoy/common/time.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "absl/functional/function_ref.h"
#include "absl/status/statusor.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

// Metadata associated with an item when it is added to a queue.
struct QueueItemMetadata {
  MonotonicTime enqueue_time_;
};

// Base class that handles queuing for objects. Implementations are free to choose any internal
// container; the interface is intentionally container-agnostic.
template <class ItemType> class QueueBase {

public:
  virtual ~QueueBase() = default;

  virtual size_t size() const PURE;

  virtual bool empty() const PURE;

  // Adds an item to the queue. The caller retains ownership and must keep the item alive until it
  // is removed with pop() or remove().
  virtual void add(ItemType& item, QueueItemMetadata metadata) PURE;

  // Returns the next item to be dequeued. It is illegal to call this on an empty queue.
  virtual ItemType& peek() const PURE;

  // Removes the next item from the queue without destroying it.
  virtual void pop() PURE;

  // Removes a specific item from the queue without destroying it.
  virtual void remove(ItemType& item) PURE;

  virtual bool isOverloaded() const PURE;

  // Iterates over the queued items in dequeue order, invoking cb for each item. Iteration stops
  // early if cb returns false. The callback is allowed to remove the visited item from the queue
  // (via remove()); implementations must make this safe.
  virtual void forEach(absl::FunctionRef<bool(ItemType&)> cb) PURE;
};

template <class ItemType> using QueuePolicyUniquePtr = std::unique_ptr<QueueBase<ItemType>>;

/**
 * Implemented by each queue policy and registered via Registry::registerFactory() or the
 * convenience class RegisterFactory.
 */
template <class ItemType> class QueuePolicyFactory : public Config::TypedFactory {
public:
  ~QueuePolicyFactory() override = default;

  /**
   * Create a particular queue policy implementation.
   * @param config supplies the configuration for the queue policy.
   * @param stat_prefix prefix for stat logging
   * @param validation_visitor validation visitor for config validation.
   * @return the queue policy unique pointer or an error status.
   */
  virtual absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.queue_policy"; }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
