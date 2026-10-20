#pragma once

#include "envoy/extensions/queue_policy/adaptive_lifo/v3/adaptive_lifo.pb.h"
#include "envoy/extensions/queue_policy/adaptive_lifo/v3/adaptive_lifo.pb.validate.h"

#include "source/common/protobuf/utility.h"
#include "source/common/queue_policy/adaptive_lifo_queue.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

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
    if (config.low_watermark() >= config.high_watermark()) {
      return absl::InvalidArgumentError(
          "adaptive LIFO low_watermark must be less than high_watermark");
    }
    return std::make_unique<AdaptiveLifoQueue<ItemType>>(config.high_watermark(),
                                                         config.low_watermark());
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
