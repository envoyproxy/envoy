#pragma once

#include <cstdint>
#include <list>
#include <vector>

#include "envoy/extensions/queue_policy/adaptive_lifo/v3/adaptive_lifo.pb.h"
#include "envoy/extensions/queue_policy/adaptive_lifo/v3/adaptive_lifo.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/protobuf/utility.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class AdaptiveLifoQueue : public QueueBase<ItemType> {
public:
  explicit AdaptiveLifoQueue(uint32_t lifo_switch_threshold)
      : lifo_switch_threshold_(lifo_switch_threshold) {
    ASSERT(lifo_switch_threshold_ > 0);
  }

  size_t size() const override { return items_.size(); }

  bool empty() const override { return items_.empty(); }

  void add(ItemType& item, QueueItemMetadata) override {
    ASSERT(item_index_.find(&item) == item_index_.end());
    item_index_.emplace(&item, items_.insert(items_.end(), &item));
  }

  const ItemType& peek() const override { return *nextItem(); }

  ItemType& peek() override { return *nextItem(); }

  ItemType& pop() override {
    ItemType& item = *nextItem();
    remove(item);
    return item;
  }

  void remove(ItemType& item) override {
    auto entry = item_index_.find(&item);
    ASSERT(entry != item_index_.end());
    items_.erase(entry->second);
    item_index_.erase(entry);
  }

  bool isOverloaded() const override { return items_.size() >= lifo_switch_threshold_; }

  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    // Snapshot the pointers in the current dequeue order so that the callback may remove the
    // visited item without invalidating this traversal.
    std::vector<ItemType*> ordered_items;
    ordered_items.reserve(items_.size());
    if (isOverloaded()) {
      ordered_items.insert(ordered_items.end(), items_.rbegin(), items_.rend());
    } else {
      ordered_items.insert(ordered_items.end(), items_.begin(), items_.end());
    }

    for (ItemType* item : ordered_items) {
      if (!cb(*item)) {
        return;
      }
    }
  }

private:
  ItemType* nextItem() const {
    ASSERT(!items_.empty());
    return isOverloaded() ? items_.back() : items_.front();
  }

  using ItemList = std::list<ItemType*>;
  const uint32_t lifo_switch_threshold_;
  ItemList items_;
  absl::flat_hash_map<ItemType*, typename ItemList::iterator> item_index_;
};

template <class ItemType> class AdaptiveLifoQueueFactory : public QueuePolicyFactory<ItemType> {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::queue_policy::adaptive_lifo::v3::AdaptiveLifoQueuePolicyConfig>();
  }

  absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor) override {
    return createQueuePolicyTyped(
        MessageUtil::downcastAndValidate<const envoy::extensions::queue_policy::adaptive_lifo::v3::
                                             AdaptiveLifoQueuePolicyConfig&>(config,
                                                                             validation_visitor),
        stat_prefix);
  }

  std::string name() const override { return "envoy.queue_policy.adaptive_lifo"; }

private:
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>> createQueuePolicyTyped(
      const envoy::extensions::queue_policy::adaptive_lifo::v3::AdaptiveLifoQueuePolicyConfig&
          config,
      const std::string&) {
    return std::make_unique<AdaptiveLifoQueue<ItemType>>(config.lifo_switch_threshold());
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
