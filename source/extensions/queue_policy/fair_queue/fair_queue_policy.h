#pragma once

#include <algorithm>
#include <array>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "envoy/extensions/queue_policy/fair_queue/v3/fair_queue.pb.h"
#include "envoy/extensions/queue_policy/fair_queue/v3/fair_queue.pb.validate.h"

#include "source/common/common/assert.h"
#include "source/common/common/hash.h"
#include "source/common/config/metadata.h"
#include "source/common/protobuf/utility.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/base/optimization.h"
#include "absl/container/flat_hash_map.h"
#include "absl/functional/function_ref.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

/**
 * Stochastic Fair Queuing (SFQ) implementation of QueueBase.
 *
 * Incoming items are hashed into two candidate buckets out of a fixed number of per-bucket FIFO
 * queues based on a dynamic metadata key read from the item's StreamInfo, and placed into the
 * shorter bucket (power of two choices). Non-empty buckets are serviced in round-robin order. The
 * hash seed is periodically perturbed based on item enqueue timestamps to prevent persistent hash
 * collisions between tenants.
 */
template <class ItemType> class FairQueue : public QueueBase<ItemType> {
public:
  static constexpr uint32_t DefaultNumBuckets = 1024;
  static constexpr uint64_t DefaultPerturbationIntervalMs = 1000;

  /**
   * Constructs a FairQueue with the specified metadata key, bucket count, and perturbation
   * interval.
   * @param class_metadata_key dynamic metadata key used to classify queued items.
   * @param num_buckets number of hash buckets used to partition queued items.
   * @param perturbation_interval time interval at which the bucket hash seed is rotated.
   */
  explicit FairQueue(const Envoy::Config::MetadataKey& class_metadata_key,
                     uint32_t num_buckets = DefaultNumBuckets,
                     std::chrono::milliseconds perturbation_interval =
                         std::chrono::milliseconds(DefaultPerturbationIntervalMs))
      : class_metadata_key_(class_metadata_key), num_buckets_(std::max(1u, num_buckets)),
        is_power_of_two_buckets_((num_buckets_ & (num_buckets_ - 1)) == 0),
        bucket_mask_(is_power_of_two_buckets_ ? (num_buckets_ - 1) : 0),
        perturbation_interval_(perturbation_interval),
        next_perturbation_time_(perturbation_interval_.count() > 0 ? MonotonicTime::min()
                                                                   : MonotonicTime::max()) {
    updateSeedCache();
  }

  ~FairQueue() override = default;

  /**
   * Returns the total number of items currently queued across all buckets.
   * @return size_t total number of queued items.
   */
  size_t size() const override { return item_index_.size(); }

  /**
   * Returns whether all buckets in the queue are empty.
   * @return bool true if no items are queued in any bucket.
   */
  bool empty() const override { return item_index_.empty(); }

  /**
   * Adds an item to the shorter of two candidate buckets corresponding to its StreamInfo class
   * metadata value.
   * @param item item to enqueue. The caller retains ownership while queued.
   * @param metadata enqueue timestamp and optional StreamInfo for class lookup.
   */
  void add(ItemType& item, QueueItemMetadata metadata) override {
    ASSERT(!item_index_.contains(&item));
    if (ABSL_PREDICT_FALSE(buckets_.empty())) {
      buckets_.resize(num_buckets_);
      bucket_iterators_.resize(num_buckets_);
    }
    maybePerturbSeed(metadata.enqueue_time_);

    const uint32_t bucket_idx = computeBucketIndex(metadata);
    ItemList& bucket = buckets_[bucket_idx];
    if (bucket.empty()) {
      bucket_iterators_[bucket_idx] = active_buckets_.insert(active_buckets_.end(), bucket_idx);
    }
    auto item_it = bucket.insert(bucket.end(), &item);
    item_index_.emplace(&item, ItemLocation{bucket_idx, item_it});
  }

  /**
   * Returns a const reference to the next item in round-robin dequeue order.
   * @return const ItemType& next item to be dequeued.
   */
  const ItemType& peek() const override {
    ASSERT(!active_buckets_.empty());
    const uint32_t bucket_idx = active_buckets_.front();
    ASSERT(!buckets_[bucket_idx].empty());
    return *buckets_[bucket_idx].front();
  }

  /**
   * Returns a mutable reference to the next item in round-robin dequeue order.
   * @return ItemType& next item to be dequeued.
   */
  ItemType& peek() override {
    ASSERT(!active_buckets_.empty());
    const uint32_t bucket_idx = active_buckets_.front();
    ASSERT(!buckets_[bucket_idx].empty());
    return *buckets_[bucket_idx].front();
  }

  /**
   * Removes and returns the front item of the current round-robin bucket and advances the turn to
   * the next active bucket.
   * @return ItemType& reference to the removed item.
   */
  ItemType& pop() override {
    ASSERT(!active_buckets_.empty());
    const uint32_t bucket_idx = active_buckets_.front();
    ASSERT(!buckets_[bucket_idx].empty());
    ItemType& item = *buckets_[bucket_idx].front();
    remove(item);
    if (!buckets_[bucket_idx].empty()) {
      active_buckets_.splice(active_buckets_.end(), active_buckets_, active_buckets_.begin());
    }
    return item;
  }

  /**
   * Removes a specific item from its bucket and deactivates the bucket if it becomes empty.
   * @param item item to remove from the queue.
   */
  void remove(ItemType& item) override {
    auto entry = item_index_.find(&item);
    ASSERT(entry != item_index_.end());
    const uint32_t bucket_idx = entry->second.bucket_idx_;
    ItemList& bucket = buckets_[bucket_idx];
    bucket.erase(entry->second.item_it_);
    item_index_.erase(entry);
    if (bucket.empty()) {
      active_buckets_.erase(bucket_iterators_[bucket_idx]);
    }
  }

  /**
   * Reports whether the queue is in an overloaded state.
   * @return bool false as FairQueue does not signal queue overload on its own.
   */
  bool isOverloaded() const override { return false; }

  /**
   * Visits queued items in round-robin dequeue order, allowing the callback to remove visited
   * items safely.
   * @param cb callback invoked for each queued item; returning false stops iteration early.
   */
  void forEach(absl::FunctionRef<bool(ItemType&)> cb) override {
    if (empty()) {
      return;
    }
    const std::vector<ItemType*> snapshot = collectItemsInDequeueOrder();
    for (ItemType* item : snapshot) {
      if (item_index_.contains(item) && !cb(*item)) {
        return;
      }
    }
  }

private:
  using ItemList = std::list<ItemType*>;
  using BucketList = std::list<uint32_t>;

  struct ItemLocation {
    uint32_t bucket_idx_;
    typename ItemList::iterator item_it_;
  };

  // Updates the cached per-kind hash seeds and default bucket index for the current seed epoch.
  void updateSeedCache() {
    for (size_t kind = 0; kind < type_seeds_.size(); ++kind) {
      type_seeds_[kind] = HashUtil::xxHash64Value(static_cast<uint32_t>(kind), perturbation_seed_);
    }
    default_bucket_idx_ = hashToBucket(type_seeds_[0]);
  }

  // Maps a 64-bit hash value to a bucket index in [0, num_buckets_).
  uint32_t hashToBucket(uint64_t hash) const {
    if (ABSL_PREDICT_TRUE(is_power_of_two_buckets_)) {
      return static_cast<uint32_t>(hash) & bucket_mask_;
    }
    return static_cast<uint32_t>(hash % num_buckets_);
  }

  // Advances the hash perturbation seed when the configured perturbation interval has elapsed.
  void maybePerturbSeed(MonotonicTime enqueue_time) {
    if (ABSL_PREDICT_TRUE(enqueue_time < next_perturbation_time_)) {
      return;
    }
    if (next_perturbation_time_ != MonotonicTime::min()) {
      ++perturbation_seed_;
      updateSeedCache();
    }
    next_perturbation_time_ = enqueue_time + perturbation_interval_;
  }

  // Maps item metadata to a bucket index in [0, num_buckets_) using two-choice selection.
  // Requests with missing or null metadata map to a single shared default bucket.
  uint32_t computeBucketIndex(const QueueItemMetadata& metadata) const {
    const std::optional<uint64_t> class_hash = computeClassHash(metadata);
    if (!class_hash.has_value()) {
      return default_bucket_idx_;
    }

    const uint32_t bucket1 = hashToBucket(*class_hash);
    if (buckets_[bucket1].empty() || num_buckets_ == 1) {
      return bucket1;
    }

    uint32_t bucket2 = hashToBucket(*class_hash >> 32);
    if (bucket2 == bucket1) {
      bucket2 = (bucket1 + 1 < num_buckets_) ? (bucket1 + 1) : 0;
    }
    return buckets_[bucket1].size() <= buckets_[bucket2].size() ? bucket1 : bucket2;
  }

  // Computes a 64-bit hash for the tenant class extracted from StreamInfo dynamic metadata,
  // or std::nullopt if the metadata is absent or null.
  std::optional<uint64_t> computeClassHash(const QueueItemMetadata& metadata) const {
    if (!metadata.stream_info_.has_value()) {
      return std::nullopt;
    }
    const Protobuf::Value& value = Envoy::Config::Metadata::metadataValue(
        &metadata.stream_info_->dynamicMetadata(), class_metadata_key_);
    switch (value.kind_case()) {
    case Protobuf::Value::kStringValue:
      return HashUtil::xxHash64(value.string_value(), type_seeds_[Protobuf::Value::kStringValue]);
    case Protobuf::Value::kNumberValue:
      return HashUtil::xxHash64Value(value.number_value(),
                                     type_seeds_[Protobuf::Value::kNumberValue]);
    case Protobuf::Value::kBoolValue:
      return HashUtil::xxHash64Value(value.bool_value(), type_seeds_[Protobuf::Value::kBoolValue]);
    case Protobuf::Value::kStructValue:
      return HashUtil::xxHash64Value(static_cast<uint64_t>(ValueUtil::hash(value)),
                                     type_seeds_[Protobuf::Value::kStructValue]);
    case Protobuf::Value::kListValue:
      return HashUtil::xxHash64Value(static_cast<uint64_t>(ValueUtil::hash(value)),
                                     type_seeds_[Protobuf::Value::kListValue]);
    case Protobuf::Value::kNullValue:
    case Protobuf::Value::KIND_NOT_SET:
      break;
    }
    return std::nullopt;
  }

  // Collects a snapshot of all queued item pointers ordered by round-robin dequeue turns.
  std::vector<ItemType*> collectItemsInDequeueOrder() const {
    std::vector<ItemType*> snapshot;
    snapshot.reserve(item_index_.size());

    struct BucketCursor {
      typename ItemList::const_iterator current_;
      typename ItemList::const_iterator end_;
    };
    std::vector<BucketCursor> cursors;
    cursors.reserve(active_buckets_.size());
    for (const uint32_t bucket_idx : active_buckets_) {
      const ItemList& bucket = buckets_[bucket_idx];
      cursors.push_back({bucket.begin(), bucket.end()});
    }

    while (!cursors.empty()) {
      std::erase_if(cursors, [&snapshot](BucketCursor& cursor) {
        snapshot.push_back(*cursor.current_);
        ++cursor.current_;
        return cursor.current_ == cursor.end_;
      });
    }
    return snapshot;
  }

  static constexpr size_t NumValueKindCases = static_cast<size_t>(Protobuf::Value::kListValue) + 1;

  const Envoy::Config::MetadataKey class_metadata_key_;
  const uint32_t num_buckets_;
  const bool is_power_of_two_buckets_;
  const uint32_t bucket_mask_;
  const std::chrono::milliseconds perturbation_interval_;
  MonotonicTime next_perturbation_time_;
  uint64_t perturbation_seed_{0};
  std::array<uint64_t, NumValueKindCases> type_seeds_{};
  uint32_t default_bucket_idx_{0};
  std::vector<ItemList> buckets_;
  BucketList active_buckets_;
  std::vector<typename BucketList::iterator> bucket_iterators_;
  absl::flat_hash_map<ItemType*, ItemLocation> item_index_;
};

/**
 * Factory for creating FairQueue instances from FairQueuePolicyConfig proto configuration.
 */
template <class ItemType> class FairQueueFactory : public QueuePolicyFactory<ItemType> {
public:
  FairQueueFactory() : QueuePolicyFactory<ItemType>() {}

  /**
   * Creates an empty FairQueuePolicyConfig proto message.
   * @return ProtobufTypes::MessagePtr empty configuration proto.
   */
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<
        envoy::extensions::queue_policy::fair_queue::v3::FairQueuePolicyConfig>();
  }

  /**
   * Validates the configuration proto and creates a FairQueue instance.
   * @param config supplies the configuration for the queue policy.
   * @param stat_prefix prefix for stat logging.
   * @param validation_visitor validation visitor for config validation.
   * @return absl::StatusOr<QueuePolicyUniquePtr<ItemType>> the created FairQueue or an error.
   */
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor) override {
    return createQueuePolicyTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::queue_policy::fair_queue::v3::FairQueuePolicyConfig&>(
            config, validation_visitor),
        stat_prefix);
  }

  /**
   * Returns the registered extension name for the fair queue policy.
   * @return std::string extension name.
   */
  std::string name() const override { return "envoy.queue_policy.fair_queue"; }

private:
  // Creates a FairQueue from a validated FairQueuePolicyConfig proto.
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>> createQueuePolicyTyped(
      const envoy::extensions::queue_policy::fair_queue::v3::FairQueuePolicyConfig& config,
      const std::string&) {
    const uint32_t num_buckets = PROTOBUF_GET_WRAPPED_OR_DEFAULT(
        config, num_buckets, FairQueue<ItemType>::DefaultNumBuckets);
    const std::chrono::milliseconds perturbation_interval(PROTOBUF_GET_MS_OR_DEFAULT(
        config, perturbation_interval, FairQueue<ItemType>::DefaultPerturbationIntervalMs));
    return std::make_unique<FairQueue<ItemType>>(
        Envoy::Config::MetadataKey(config.class_metadata_key()), num_buckets,
        perturbation_interval);
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
