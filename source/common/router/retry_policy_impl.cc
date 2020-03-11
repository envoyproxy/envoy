#include "common/router/retry_policy_impl.h"

#include "envoy/http/header_map.h"

#include "common/config/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/header_utility.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/protobuf.h"
#include "common/protobuf/utility.h"

namespace Envoy {
namespace Router {

// These are defined in envoy/router/router.h, however during certain cases the compiler is
// refusing to use the header version so allocate space here.
const uint32_t CoreRetryPolicy::RETRY_ON_5XX;
const uint32_t CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR;
const uint32_t CoreRetryPolicy::RETRY_ON_CONNECT_FAILURE;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_4XX;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
const uint32_t CoreRetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
const uint32_t CoreRetryPolicy::RETRY_ON_RESET;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
const uint32_t CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;

std::pair<uint32_t, bool> RetryPolicyImpl::parseRetryOn(absl::string_view config) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(config, ",")) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnValues._5xx) {
      ret |= CoreRetryPolicy::RETRY_ON_5XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.GatewayError) {
      ret |= CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.ConnectFailure) {
      ret |= CoreRetryPolicy::RETRY_ON_CONNECT_FAILURE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Retriable4xx) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_4XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RefusedStream) {
      ret |= CoreRetryPolicy::RETRY_ON_REFUSED_STREAM;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableStatusCodes) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableHeaders) {
      ret |= CoreRetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Reset) {
      ret |= CoreRetryPolicy::RETRY_ON_RESET;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

std::pair<uint32_t, bool>
RetryPolicyImpl::parseRetryGrpcOn(absl::string_view retry_grpc_on_header) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(retry_grpc_on_header, ",")) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Cancelled) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.DeadlineExceeded) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.ResourceExhausted) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Unavailable) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Internal) {
      ret |= CoreRetryPolicy::RETRY_ON_GRPC_INTERNAL;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

RetryPolicyImpl::RetryPolicyImpl(const envoy::config::route::v3::RetryPolicy& retry_policy,
                                 ProtobufMessage::ValidationVisitor& validation_visitor)
    : per_try_timeout_(
          std::chrono::milliseconds(PROTOBUF_GET_MS_OR_DEFAULT(retry_policy, per_try_timeout, 0))),
      num_retries_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(retry_policy, num_retries, 1)),
      remaining_retries_(num_retries_),
      retriable_status_codes_(retry_policy.retriable_status_codes().begin(),
                              retry_policy.retriable_status_codes().end()),
      retriable_headers_(
          Http::HeaderUtility::buildHeaderMatcherVector(retry_policy.retriable_headers())),
      retriable_request_headers_(
          Http::HeaderUtility::buildHeaderMatcherVector(retry_policy.retriable_request_headers())),
      validation_visitor_(&validation_visitor) {
  retry_on_ = parseRetryOn(retry_policy.retry_on()).first;
  retry_on_ |= parseRetryGrpcOn(retry_policy.retry_on()).first;

  for (const auto& host_predicate : retry_policy.retry_host_predicate()) {
    auto& factory = Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryHostPredicateFactory>(
        host_predicate);
    auto config = Envoy::Config::Utility::translateToFactoryConfig(host_predicate,
                                                                   validation_visitor, factory);
    retry_host_predicate_configs_.emplace_back(factory, std::move(config));
  }

  const auto& retry_priority = retry_policy.retry_priority();
  if (!retry_priority.name().empty()) {
    auto& factory =
        Envoy::Config::Utility::getAndCheckFactory<Upstream::RetryPriorityFactory>(retry_priority);
    retry_priority_config_ =
        std::make_pair(&factory, Envoy::Config::Utility::translateToFactoryConfig(
                                     retry_priority, validation_visitor, factory));
  }

  auto host_selection_attempts = retry_policy.host_selection_retry_max_attempts();
  if (host_selection_attempts) {
    host_selection_attempts_ = host_selection_attempts;
  }

  if (retry_policy.has_retry_back_off()) {
    base_interval_ = std::chrono::milliseconds(
        PROTOBUF_GET_MS_REQUIRED(retry_policy.retry_back_off(), base_interval));
    if ((*base_interval_).count() < 1) {
      base_interval_ = std::chrono::milliseconds(1);
    }

    max_interval_ = PROTOBUF_GET_OPTIONAL_MS(retry_policy.retry_back_off(), max_interval);
    if (max_interval_) {
      // Apply the same rounding to max interval in case both are set to sub-millisecond values.
      if ((*max_interval_).count() < 1) {
        max_interval_ = std::chrono::milliseconds(1);
      }

      if ((*max_interval_).count() < (*base_interval_).count()) {
        throw EnvoyException(
            "retry_policy.max_interval must greater than or equal to the base_interval");
      }
    }
  }
}

void RetryPolicyImpl::recordRequestHeader(Http::RequestHeaderMap& request_headers) {
  std::cout << retry_on_ << std::endl;
  // Merge in the headers.
  if (request_headers.EnvoyRetryOn()) {
    retry_on_ |= parseRetryOn(request_headers.EnvoyRetryOn()->value().getStringView()).first;
  }
  if (request_headers.EnvoyRetryGrpcOn()) {
    retry_on_ |=
        parseRetryGrpcOn(request_headers.EnvoyRetryGrpcOn()->value().getStringView()).first;
  }
  std::cout << retry_on_ << std::endl;

  if (!retriable_request_headers_.empty()) {
    // If this route limits retries by request headers, make sure there is a match.
    bool request_header_match = false;
    for (const auto& retriable_header : retriable_request_headers_) {
      if (retriable_header->matchesHeaders(request_headers)) {
        request_header_match = true;
        break;
      }
    }

    if (!request_header_match) {
      retry_on_ = 0;
    }
  }
  if (retry_on_ != 0 && request_headers.EnvoyMaxRetries()) {
    uint64_t temp;
    if (absl::SimpleAtoi(request_headers.EnvoyMaxRetries()->value().getStringView(), &temp)) {
      // The max retries header takes precedence if set.
      num_retries_ = remaining_retries_ = temp;
    }
  }

  if (request_headers.EnvoyRetriableStatusCodes()) {
    for (const auto code : StringUtil::splitToken(
             request_headers.EnvoyRetriableStatusCodes()->value().getStringView(), ",")) {
      unsigned int out;
      if (absl::SimpleAtoi(code, &out)) {
        retriable_status_codes_.emplace_back(out);
      }
    }
  }

  if (request_headers.EnvoyRetriableHeaderNames()) {
    // Retriable headers in the configuration are specified via HeaderMatcher.
    // Giving the same flexibility via request header would require the user
    // to provide HeaderMatcher serialized into a string. To avoid this extra
    // complexity we only support name-only header matchers via request
    // header. Anything more sophisticated needs to be provided via config.
    for (const auto header_name : StringUtil::splitToken(
             request_headers.EnvoyRetriableHeaderNames()->value().getStringView(), ",")) {
      envoy::config::route::v3::HeaderMatcher header_matcher;
      header_matcher.set_name(std::string(absl::StripAsciiWhitespace(header_name)));
      retriable_headers_.emplace_back(
          std::make_shared<Http::HeaderUtility::HeaderData>(header_matcher));
    }
  }
}

bool RetryPolicyImpl::wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers) const {
  if (response_headers.EnvoyOverloaded() != nullptr) {
    return false;
  }

  // We never retry if the request is rate limited.
  if (response_headers.EnvoyRateLimited() != nullptr) {
    return false;
  }

  if (retry_on_ & CoreRetryPolicy::RETRY_ON_5XX) {
    if (Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if (retry_on_ & CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR) {
    if (Http::CodeUtility::isGatewayError(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if ((retry_on_ & CoreRetryPolicy::RETRY_ON_RETRIABLE_4XX)) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(response_headers));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  if ((retry_on_ & CoreRetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES)) {
    for (auto code : retriable_status_codes_) {
      if (Http::Utility::getResponseStatus(response_headers) == code) {
        return true;
      }
    }
  }

  if (retry_on_ & CoreRetryPolicy::RETRY_ON_RETRIABLE_HEADERS) {
    for (const auto& retriable_header : retriable_headers_) {
      if (retriable_header->matchesHeaders(response_headers)) {
        return true;
      }
    }
  }

  if (retry_on_ &
      (CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED | CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
       CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED |
       CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE | CoreRetryPolicy::RETRY_ON_GRPC_INTERNAL)) {
    absl::optional<Grpc::Status::GrpcStatus> status = Grpc::Common::getGrpcStatus(response_headers);
    if (status) {
      if ((status.value() == Grpc::Status::Canceled &&
           (retry_on_ & CoreRetryPolicy::RETRY_ON_GRPC_CANCELLED)) ||
          (status.value() == Grpc::Status::DeadlineExceeded &&
           (retry_on_ & CoreRetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED)) ||
          (status.value() == Grpc::Status::ResourceExhausted &&
           (retry_on_ & CoreRetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED)) ||
          (status.value() == Grpc::Status::Unavailable &&
           (retry_on_ & CoreRetryPolicy::RETRY_ON_GRPC_UNAVAILABLE)) ||
          (status.value() == Grpc::Status::Internal &&
           (retry_on_ & CoreRetryPolicy::RETRY_ON_GRPC_INTERNAL))) {
        return true;
      }
    }
  }

  return false;
}

bool RetryPolicyImpl::wouldRetryFromReset(Http::StreamResetReason reset_reason) const {
  // First check "never retry" conditions so we can short circuit (we never
  // retry if the reset reason is overflow).
  if (reset_reason == Http::StreamResetReason::Overflow) {
    return false;
  }

  if (retry_on_ & CoreRetryPolicy::RETRY_ON_RESET) {
    return true;
  }

  if (retry_on_ & (CoreRetryPolicy::RETRY_ON_5XX | CoreRetryPolicy::RETRY_ON_GATEWAY_ERROR)) {
    // Currently we count an upstream reset as a "5xx" (since it will result in
    // one). With RETRY_ON_RESET we may eventually remove these policies.
    return true;
  }

  if ((retry_on_ & CoreRetryPolicy::RETRY_ON_REFUSED_STREAM) &&
      reset_reason == Http::StreamResetReason::RemoteRefusedStreamReset) {
    return true;
  }

  if ((retry_on_ & CoreRetryPolicy::RETRY_ON_CONNECT_FAILURE) &&
      reset_reason == Http::StreamResetReason::ConnectionFailure) {
    return true;
  }

  return false;
}

std::vector<Upstream::RetryHostPredicateSharedPtr> RetryPolicyImpl::retryHostPredicates() const {
  std::vector<Upstream::RetryHostPredicateSharedPtr> predicates;

  for (const auto& config : retry_host_predicate_configs_) {
    predicates.emplace_back(config.first.createHostPredicate(*config.second, num_retries_));
  }

  return predicates;
}

Upstream::RetryPrioritySharedPtr RetryPolicyImpl::retryPriority() const {
  if (retry_priority_config_.first == nullptr) {
    return nullptr;
  }

  return retry_priority_config_.first->createRetryPriority(*retry_priority_config_.second,
                                                           *validation_visitor_, num_retries_);
}

} // namespace Router
} // namespace Envoy