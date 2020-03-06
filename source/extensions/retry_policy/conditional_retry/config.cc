#include "extensions/retry_policy/conditional_retry/config.h"

#include "envoy/extensions/retry_policy/conditional_retry/v3/conditional_retry.pb.validate.h"
#include "envoy/registry/registry.h"

#include "common/http/header_utility.h"
#include "common/router/retry_state_impl.h"

#include "extensions/retry_policy/conditional_retry/conditional_retry.h"

namespace Envoy {
namespace Extensions {
namespace RetryPolicy {

Router::RetryPolicyExtensionSharedPtr
ConditionalRetryFactory::createRetryPolicy(const Protobuf::Message& config,
                                           const Http::RequestHeaderMap& request_header,
                                           ProtobufMessage::ValidationVisitor& validation_visitor) {

  auto& message = MessageUtil::downcastAndValidate<
      const envoy::extensions::retry_policy::conditional_retry::v3::ConditionalRetry&>(
      config, validation_visitor);

  uint32_t retry_on{0};
  std::vector<uint32_t> retriable_status_codes;
  for (const auto& condition : message.retry_conditions()) {
    auto header_data =
        std::make_unique<Http::HeaderUtility::HeaderData>(condition.request_header());

    // Find the first match and break.
    if (Http::HeaderUtility::matchHeaders(request_header, *header_data)) {
      retry_on = Router::RetryStateImpl::parseRetryOn(condition.retry_on()).first;
      retry_on |= Router::RetryStateImpl::parseRetryGrpcOn(condition.retry_on()).first;

      retriable_status_codes.assign(condition.retriable_status_codes().begin(),
                                    condition.retriable_status_codes().end());
      break;
    }
  }

  return std::make_shared<ConditionalRetry>(retry_on, retriable_status_codes);
}

REGISTER_FACTORY(ConditionalRetryFactory, Router::RetryPolicyFactory);

} // namespace RetryPolicy
} // namespace Extensions
} // namespace Envoy