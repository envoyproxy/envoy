#pragma once

#include <chrono>
#include <utility>

#include "envoy/extensions/queue_policy/codel/v3/codel.pb.h"
#include "envoy/extensions/queue_policy/codel/v3/codel.pb.validate.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"
#include "source/common/queue_policy/codel_queue.h"
#include "source/common/queue_policy/fifo_queue_policy.h"
#include "source/common/queue_policy/queue_policy_base.h"

#include "absl/status/status.h"
#include "fmt/format.h"

namespace Envoy {
namespace Extensions {
namespace QueuePolicy {

template <class ItemType> class CodelQueueFactory : public QueuePolicyFactory<ItemType> {
public:
  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return std::make_unique<envoy::extensions::queue_policy::codel::v3::CodelQueuePolicyConfig>();
  }

  absl::StatusOr<QueuePolicyUniquePtr<ItemType>>
  createQueuePolicy(const Protobuf::Message& config, const std::string& stat_prefix,
                    ProtobufMessage::ValidationVisitor& validation_visitor,
                    MaxQueueSizeFn max_queue_size) override {
    return createQueuePolicyTyped(
        MessageUtil::downcastAndValidate<
            const envoy::extensions::queue_policy::codel::v3::CodelQueuePolicyConfig&>(
            config, validation_visitor),
        stat_prefix, validation_visitor, std::move(max_queue_size));
  }

  std::string name() const override { return "envoy.queue_policy.codel"; }

private:
  absl::StatusOr<QueuePolicyUniquePtr<ItemType>> createQueuePolicyTyped(
      const envoy::extensions::queue_policy::codel::v3::CodelQueuePolicyConfig& config,
      const std::string& stat_prefix, ProtobufMessage::ValidationVisitor& validation_visitor,
      MaxQueueSizeFn max_queue_size) {
    const std::chrono::milliseconds target_delay(
        config.has_target_delay() ? DurationUtil::durationToMilliseconds(config.target_delay())
                                  : 5);
    const std::chrono::milliseconds interval(
        config.has_interval() ? DurationUtil::durationToMilliseconds(config.interval()) : 100);
    if (interval <= target_delay) {
      return absl::InvalidArgumentError("CoDel interval must be greater than target_delay");
    }

    QueuePolicyUniquePtr<ItemType> underlying_queue;
    if (!config.has_underlying_queue_policy()) {
      underlying_queue = std::make_unique<FifoQueue<ItemType>>();
    } else {
      const auto& policy_config = config.underlying_queue_policy();
      QueuePolicyFactory<ItemType>* factory =
          Config::Utility::getFactory<QueuePolicyFactory<ItemType>>(policy_config);
      if (factory == nullptr) {
        factory =
            Config::Utility::getFactoryByName<QueuePolicyFactory<ItemType>>(policy_config.name());
      }
      if (factory == nullptr) {
        return absl::InvalidArgumentError(
            fmt::format("Didn't find a registered queue policy implementation for name: '{}'",
                        policy_config.name()));
      }
      if (factory->name() == name()) {
        return absl::InvalidArgumentError("CoDel queue policies cannot be nested");
      }

      ProtobufTypes::MessagePtr factory_config = factory->createEmptyConfigProto();
      RETURN_IF_NOT_OK(Config::Utility::translateOpaqueConfig(policy_config.typed_config(),
                                                              validation_visitor, *factory_config));
      auto queue = factory->createQueuePolicy(*factory_config, stat_prefix, validation_visitor,
                                              std::move(max_queue_size));
      RETURN_IF_NOT_OK(queue.status());
      underlying_queue = std::move(queue.value());
    }

    return std::make_unique<CodelQueue<ItemType>>(std::move(underlying_queue), target_delay,
                                                  interval);
  }
};

} // namespace QueuePolicy
} // namespace Extensions
} // namespace Envoy
