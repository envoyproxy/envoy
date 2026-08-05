#pragma once

#include "envoy/extensions/queue_policy/fifo/v3/fifo.pb.h"
#include "envoy/extensions/queue_policy/fifo/v3/fifo.pb.validate.h"

#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/queue_policy/fifo_queue_policy.h"
#include "source/common/queue_policy/queue_policy_base.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class FifoQueueFactory : public QueuePolicyFactory<ItemType> {
public:
  FifoQueueFactory() : QueuePolicyFactory<ItemType>() {}

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig>();
  }

  // QueuePolicyFactory
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor) override {
    return createQueuePolicyTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig&>(
            config, validation_visitor),
        stat_prefix);
  }

  std::string name() const override { return "envoy.queue_policy.fifo"; }

private:
  // QueuePolicyFactory
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicyTyped(const envoy::extensions::queue_policy::fifo::v3::FifoQueuePolicyConfig&,
                         const std::string&) {
    return std::make_unique<FifoQueue<ItemType>>();
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
