#pragma once

#include "source/extensions/queue_strategy/common/queue_strategy_base.h"
#include "source/common/protobuf/message_validator_impl.h"

#include "envoy/config/cluster/v3/cluster.pb.h"
#include "envoy/config/cluster/v3/cluster.pb.validate.h"

namespace Envoy {
namespace Extensions {
namespace QueueStrategy {

    template <class ItemType> class FifoQueue : public virtual Extensions::QueueStrategy::QueueBase<ItemType> {

    using ItemPtrType = std::unique_ptr<ItemType>;
    using Iterator = typename QueueBase<ItemType>::Iterator;

    public:
      FifoQueue() = default;
      virtual ~FifoQueue() = default;

      const ItemPtrType& next() const override { return this->items_.back(); }

      bool isOverloaded() const override {
    return false;
    }

      Iterator begin() override {
        auto it  = this->items_.rend();
        if (!this->items_.empty()) {
          ++it;
        }
        return Iterator(std::move(it));
      }

      Iterator end() override {
        auto it  = this->items_.begin();
        return Iterator(std::move(it));
      }

    };

template <class ItemType>
class FifoQueueFactory: public QueueStrategyFactory<ItemType> {
public:
  FifoQueueFactory(): QueueStrategyFactory<ItemType>() {}

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::config::cluster::v3::Cluster::FifoQueueStrategyConfig>();
  }



// QueueStrategyFactory
absl::StatusOr<QueueStrategySharedPtr<ItemType>>
  createQueueStrategy(const Protobuf::Message& config, const std::string& stat_prefix,
                            Server::Configuration::FactoryContext& factory_context) override  {
    return createQueueStrategyTyped(MessageUtil::downcastAndValidate<const envoy::config::cluster::v3::Cluster::FifoQueueStrategyConfig&>(
                                              config, factory_context.messageValidationVisitor()), stat_prefix, factory_context);
}

  std::string name() const override { return "envoy.queue_strategy.fifo"; }

private:
  // QueueStrategyFactory
absl::StatusOr<QueueStrategySharedPtr<ItemType>>
  createQueueStrategyTyped(const envoy::config::cluster::v3::Cluster::FifoQueueStrategyConfig&, const std::string&,
                            Server::Configuration::FactoryContext&){
  return std::make_shared<FifoQueue<ItemType>>();
}
};

DECLARE_FACTORY(FifoQueueFactory);

/**
 * Static registration for the FifoQueueFactory.
 */
template <class ItemType> static auto REGISTER_FACTORY =                                                 \
        new Envoy::Registry::RegisterFactory<FifoQueueFactory<ItemType>, QueueStrategy::QueueStrategyFactory<ItemType>>();

} // namespace QueueStrategy
} // namespace Extensions
} // namespace Envoy