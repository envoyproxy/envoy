#pragma once

#include <list>

#include "envoy/common/conn_pool.h"
#include "envoy/server/factory_context.h"
#include "source/common/common/linked_object.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {


// Base class that handles queuing for objects.
template <class ItemType> class QueueBase {

  // static_assert(std::is_base_of<ConnectionPool::Cancellable, ItemType>::value,
  //             "Queue item type must inherit from ConnectionPool::Cancellable");

  // static_assert(std::is_base_of<LinkedObject<ItemType>, ItemType>::value,
  //           "Queue item type must inherit from LinkedObject");

  using ItemPtrType = std::unique_ptr<ItemType>;
  using ListType = std::list<ItemPtrType>;

  public:
    QueueBase() = default;
    virtual ~QueueBase() = default;

    virtual uint32_t size() const { return items_.size(); }

    virtual bool empty() const { return items_.empty(); }

    virtual ConnectionPool::Cancellable* add(ItemPtrType&& item){
    LinkedList::moveIntoList(std::move(item), items_);
    return items_.front().get();
  }

   ItemPtrType remove(ItemType& item) { return item.removeFromList(items_); }

  // Move constructor.
  QueueBase(QueueBase<ItemType>&& other) noexcept
    : items_(std::move(other.items_)) {}

QueueBase<ItemType>& operator=(QueueBase<ItemType>&& other) noexcept {
    if (this != &other) {
        items_ = std::move(other.items_);
    }
    return *this;
}

operator std::list<ItemPtrType> &&() { return std::move(items_); }

  virtual const ItemPtrType& next() const PURE;
  virtual bool isOverloaded() const PURE;

class Iterator {
public:
  Iterator(QueueBase::ListType::iterator&& itor) : itor_(std::move(itor)) {}
  Iterator(QueueBase::ListType::reverse_iterator&& itor) : itor_(std::move(itor)) {}

  Iterator& operator++() {
  if (absl::holds_alternative<typename QueueBase::ListType::iterator>(itor_)) {
      ++(absl::get<typename QueueBase::ListType::iterator>(itor_));
      return *this;
    } else {
      ++(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
      return *this;
    }
  }
  Iterator& operator--() {
  if (absl::holds_alternative<typename QueueBase::ListType::iterator>(itor_)) {
      --(absl::get<typename QueueBase::ListType::iterator>(itor_));
      return *this;
    } else {
      --(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
      return *this;
    }
  }
  ItemPtrType& operator*() const {
    if (absl::holds_alternative<typename QueueBase::ListType::iterator>(itor_)) {
      return *(absl::get<typename QueueBase::ListType::iterator>(itor_));
    } else {
      return *(absl::get<typename QueueBase::ListType::reverse_iterator>(itor_));
    }
  }
  bool operator==(const Iterator& other) const {
    return other.itor_ == itor_;
  }
  bool operator!=(const Iterator& other) const {
    return other.itor_ != itor_;
  }

  private:
  absl::variant<typename QueueBase::ListType::iterator, typename QueueBase::ListType::reverse_iterator> itor_;
};

  virtual Iterator begin() PURE;
  virtual Iterator end() PURE;

protected:
  std::list<ItemPtrType> items_;
};

template <class ItemType> using QueueStrategySharedPtr = std::shared_ptr<QueueBase<ItemType>>;

/**
 * Implemented by each queue strategy and registered via Registry::registerFactory() or the
 * convenience class RegisterFactory.
 */
template <class ItemType> class QueueStrategyFactory : public Config::TypedFactory {
public:
  ~QueueStrategyFactory() override = default;

  /**
   * Create a particular queue strategy implementation.
   * @param config supplies the configuration for the queue strategy.
   * @param stat_prefix prefix for stat logging
   * @param context supplies the context for queue strategy.
   * @return FilterFactoryCb the factory creation function.
   */
  virtual absl::StatusOr<QueueStrategySharedPtr<ItemType>>
  createQueueStrategy(const Protobuf::Message& config, const std::string& stat_prefix,
                            Server::Configuration::FactoryContext& context) PURE;

  std::string category() const override { return "envoy.queue_strategy"; }
};

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy