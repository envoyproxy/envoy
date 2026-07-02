#pragma once

#include "envoy/extensions/queue_strategy/fifo/v3/fifo.pb.h"
#include "envoy/extensions/queue_strategy/fifo/v3/fifo.pb.validate.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/queue_strategy/queue_strategy_base.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {

template <class ItemType> class FifoQueue : public Extensions::QueueStrategy::QueueBase<ItemType> {

  using ItemPtrType = std::unique_ptr<ItemType>;
  using Iterator = typename QueueBase<ItemType>::Iterator;

public:
  FifoQueue() = default;
  virtual ~FifoQueue() = default;

  const ItemPtrType& next() const override { return this->items_.back(); }

  bool isOverloaded() const override { return false; }

  Iterator begin() override {
    auto it = this->items_.rbegin();
    return Iterator(std::move(it));
  }

  Iterator end() override {
    auto it = this->items_.rend();
    return Iterator(std::move(it));
  }
};

template <class ItemType> class FifoQueueFactory : public QueueStrategyFactory<ItemType> {
public:
  FifoQueueFactory() : QueueStrategyFactory<ItemType>() {}

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::queue_strategy::fifo::v3::FifoQueueStrategyConfig>();
  }

  // QueueStrategyFactory
  absl::StatusOr<QueueStrategySharedPtr<ItemType>>
  createQueueStrategy(const Protobuf::Message& config, const std::string& stat_prefix,
                      ProtobufMessage::ValidationVisitor& validation_visitor) override {
    return createQueueStrategyTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::queue_strategy::fifo::v3::FifoQueueStrategyConfig&>(
            config, validation_visitor),
        stat_prefix);
  }

  std::string name() const override { return "envoy.queue_strategy.fifo"; }

private:
  // QueueStrategyFactory
  absl::StatusOr<QueueStrategySharedPtr<ItemType>> createQueueStrategyTyped(
      const envoy::extensions::queue_strategy::fifo::v3::FifoQueueStrategyConfig&,
      const std::string&) {
    return std::make_shared<FifoQueue<ItemType>>();
  }
};

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy
