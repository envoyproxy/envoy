#pragma once

#include <list>
#include <memory>
#include <type_traits>
#include <utility>

#include "envoy/common/conn_pool.h"
#include "envoy/config/typed_config.h"
#include "envoy/protobuf/message_validator.h"

#include "source/common/common/linked_object.h"

#include "absl/status/statusor.h"
#include "absl/types/variant.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

// Base class that handles queuing for objects.
template <class ItemType> class QueueBase {

  static_assert(std::is_base_of<ConnectionPool::Cancellable, ItemType>::value,
                "Queue item type must inherit from ConnectionPool::Cancellable");
  static_assert(std::is_base_of<LinkedObject<ItemType>, ItemType>::value,
                "Queue item type must inherit from LinkedObject");

  using ItemPtrType = std::unique_ptr<ItemType>;
  using ListType = std::list<ItemPtrType>;

public:
  QueueBase() = default;
  virtual ~QueueBase() = default;

  virtual size_t size() const { return items_.size(); }

  virtual bool empty() const { return items_.empty(); }

  virtual ConnectionPool::Cancellable* add(ItemPtrType&& item) PURE;

  virtual ItemPtrType remove(ItemType& item) PURE;

  ListType takeItems() { return std::move(items_); }

  virtual const ItemPtrType& next() const PURE;
  virtual bool isOverloaded() const PURE;

  class Iterator {
  public:
    Iterator(QueueBase::ListType::iterator&& itor) : itor_(std::move(itor)) {}
    Iterator(QueueBase::ListType::reverse_iterator&& itor) : itor_(std::move(itor)) {}

    Iterator& operator++() {
      if (auto* it = absl::get_if<typename QueueBase::ListType::iterator>(&itor_)) {
        ++(*it);
      } else {
        ++(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
      }
      return *this;
    }

    Iterator& operator--() {
      if (auto* it = absl::get_if<typename QueueBase::ListType::iterator>(&itor_)) {
        --(*it);
      } else {
        --(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
      }
      return *this;
    }

    ItemPtrType& operator*() const {
      if (auto* it = absl::get_if<typename QueueBase::ListType::iterator>(&itor_)) {
        return **it;
      } else {
        return *(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
      }
    }
    bool operator==(const Iterator& other) const { return other.itor_ == itor_; }
    bool operator!=(const Iterator& other) const { return other.itor_ != itor_; }

  private:
    absl::variant<typename QueueBase::ListType::iterator,
                  typename QueueBase::ListType::reverse_iterator>
        itor_;
  };

  virtual Iterator begin() PURE;
  virtual Iterator end() PURE;

protected:
  QueueBase(QueueBase<ItemType>&& other) noexcept : items_(std::move(other.items_)) {}

  QueueBase<ItemType>& operator=(QueueBase<ItemType>&& other) noexcept {
    if (this != &other) {
      items_ = std::move(other.items_);
    }
    return *this;
  }

  std::list<ItemPtrType> items_;
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
   * @param context supplies the context for queue policy.
   * @return FilterFactoryCb the factory creation function.
   */
  virtual absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor) PURE;

  std::string category() const override { return "envoy.queue_policy"; }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
