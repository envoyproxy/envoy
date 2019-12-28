#pragma once

#include "envoy/config/retry/previous_priorities/previous_priorities_config.pb.h"
#include "envoy/upstream/retry.h"

#include "common/protobuf/protobuf.h"

#include "extensions/retry/priority/previous_priorities/previous_priorities.h"
#include "extensions/retry/priority/well_known_names.h"

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

  std::string name() const override {
    return RetryPriorityValues::get().PreviousPrioritiesRetryPriority;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(
        new envoy::config::retry::previous_priorities::PreviousPrioritiesConfig());
  }
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
