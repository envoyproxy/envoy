#pragma once

#include "envoy/extensions/retry/priority/header_order/v3/header_order.pb.h"
#include "envoy/upstream/retry.h"

#include "source/common/protobuf/protobuf.h"
#include "source/extensions/retry/priority/header_order/header_order.h"

namespace Envoy {
namespace Extensions {
namespace Retry {
namespace Priority {

class HeaderOrderRetryPriorityFactory : public Upstream::RetryPriorityFactory {
public:
  Upstream::RetryPrioritySharedPtr
  createRetryPriority(const Protobuf::Message& config,
                      ProtobufMessage::ValidationVisitor& validation_visitor,
                      uint32_t max_retries) override;

  std::string name() const override { return "envoy.retry_priorities.header_order"; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr(
        new envoy::extensions::retry::priority::header_order::v3::HeaderOrderConfig());
  }
};

} // namespace Priority
} // namespace Retry
} // namespace Extensions
} // namespace Envoy
