#pragma once

#include "envoy/upstream/retry.h"

#include "common/protobuf/protobuf.h"

#include "extensions/retry/priority/other_priority/other_priority.h"
#include "extensions/retry/priority/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class OtherPriorityRetryPriorityFactory : public Upstream::RetryPriorityFactory {
public:
  void createRetryPriority(Upstream::RetryPriorityFactoryCallbacks& callbacks,
                           const Protobuf::Message& config, uint32_t max_retries) override;

  std::string name() const override {
    return RetryPriorityValues::get().PreviousPrioritiesRetryPriority;
  }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(new ::Envoy::ProtobufWkt::Empty());
  }
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
