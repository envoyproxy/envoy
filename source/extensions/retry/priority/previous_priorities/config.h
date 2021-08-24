#pragma once

#include "envoy/extensions/retry/priority/previous_priorities/v3/previous_priorities_config.pb.h"
#include "envoy/upstream/retry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/retry/priority/previous_priorities/previous_priorities.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class PreviousPrioritiesRetryPriorityFactory : public Upstream::RetryPriorityFactory {
public:
  Upstream::RetryPrioritySharedPtr
  createRetryPriority(const Protobuf::Message& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor,
                      uint32_t max_retries) override;

  std::string name() const override { return "envoy.retry_priorities.previous_priorities"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(new envoy::extensions::retry::priority::previous_priorities::
                                         v3::PreviousPrioritiesConfig());
  }
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
