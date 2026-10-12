#pragma once

#include <utility>

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
                    ProtobufMessage::ValidationVisitor& validation_visitor,
                    MaxQueueSizeFn max_queue_size) override {
    return createQueuePolicyTyped(
        MessageUtil::downcastAndValidate<const envoy::extensions::queue_policy::adaptive_lifo::v3::
                                             AdaptiveLifoQueuePolicyConfig&>(config,
                                                                             validation_visitor),
        stat_prefix, std::move(max_queue_size));
  }

  std::string name() const override { return "envoy.queue_policy.adaptive_lifo"; }

private:
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>> createQueuePolicyTyped(
      const envoy::extensions::queue_policy::adaptive_lifo::v3::AdaptiveLifoQueuePolicyConfig&
          config,
      const std::string&, MaxQueueSizeFn max_queue_size) {
    const double high_watermark = config.high_watermark().value();
    const double low_watermark = config.low_watermark().value();
    if (high_watermark <= 0 || low_watermark >= high_watermark) {
      return absl::InvalidArgumentError("adaptive LIFO high_watermark must be greater than zero "
                                        "and low_watermark must be less than high_watermark");
    }
    return std::make_unique<AdaptiveLifoQueue<ItemType>>(high_watermark, low_watermark,
                                                         std::move(max_queue_size));
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
