#pragma once

#include "envoy/extensions/retry_policy/conditional_retry/v3/conditional_retry.pb.h"
#include "envoy/router/router.h"

#include "common/protobuf/protobuf.h"

#include "extensions/retry_policy/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {

class ConditionalRetryFactory : public Router::RetryPolicyFactory {
public:
  Router::RetryPolicyExtensionSharedPtr
  createRetryPolicy(const Protobuf::Message& config, const Http::RequestHeaderMap& request_header,
                    ProtobufMessage::ValidationVisitor& validation_visitor) override;

  std::string name() const override { return RetryPolicyValues::get().ConditionalRetry; }

  ProtobufTypes::MessagePtr createEmptyConfigProto() override {
    return ProtobufTypes::MessagePtr{
        new envoy::extensions::retry_policy::conditional_retry::v3::ConditionalRetry()};
  }
};

} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy