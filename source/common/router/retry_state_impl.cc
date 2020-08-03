#include "common/router/retry_state_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "envoy/config/route/v3/route_components.pb.h"

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/runtime/runtime_features.h"

namespace Envoy {
namespace Router {

// These are defined in envoy/router/router.h, however during certain cases the compiler is
// refusing to use the header version so allocate space here.
const uint32_t RetryPolicy::RETRY_ON_5XX;
const uint32_t RetryPolicy::RETRY_ON_GATEWAY_ERROR;
const uint32_t RetryPolicy::RETRY_ON_CONNECT_FAILURE;
const uint32_t RetryPolicy::RETRY_ON_ENVOY_RATE_LIMITED;
const uint32_t RetryPolicy::RETRY_ON_RETRIABLE_4XX;
const uint32_t RetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
const uint32_t RetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
const uint32_t RetryPolicy::RETRY_ON_RESET;
const uint32_t RetryPolicy::RETRY_ON_GRPC_CANCELLED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;

RetryStatePtr RetryStateImpl::create(const RetryPolicy& route_policy,
                                     Http::RequestHeaderMap& request_headers,
                                     const Upstream::ClusterInfo& cluster,
                                     const VirtualCluster* vcluster, Runtime::Loader& runtime,
                                     Random::RandomGenerator& random, Event::Dispatcher& dispatcher,
                                     Upstream::ResourcePriority priority) {
  RetryStatePtr ret;

  // We short circuit here and do not bother with an allocation if there is no chance we will retry.
  if (request_headers.EnvoyRetryOn() || request_headers.EnvoyRetryGrpcOn() ||
      route_policy.retryOn()) {
    ret.reset(new RetryStateImpl(route_policy, request_headers, cluster, vcluster, runtime, random,
                                 dispatcher, priority));
  }

  // Consume all retry related headers to avoid them being propagated to the upstream
  request_headers.removeEnvoyRetryOn();
  request_headers.removeEnvoyRetryGrpcOn();
  request_headers.removeEnvoyMaxRetries();
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.consume_all_retry_headers")) {
    request_headers.removeEnvoyHedgeOnPerTryTimeout();
    request_headers.removeEnvoyRetriableHeaderNames();
    request_headers.removeEnvoyRetriableStatusCodes();
    request_headers.removeEnvoyUpstreamRequestPerTryTimeoutMs();
  }

  return ret;
}

RetryStateImpl::RetryStateImpl(const RetryPolicy& route_policy,
                               Http::RequestHeaderMap& request_headers,
                               const Upstream::ClusterInfo& cluster, const VirtualCluster* vcluster,
                               Runtime::Loader& runtime, Random::RandomGenerator& random,
                               Event::Dispatcher& dispatcher, Upstream::ResourcePriority priority)
    : cluster_(cluster), vcluster_(vcluster), runtime_(runtime), random_(random),
      dispatcher_(dispatcher), retry_on_(route_policy.retryOn()),
      retries_remaining_(route_policy.numRetries()), priority_(priority),
      retry_host_predicates_(route_policy.retryHostPredicates()),
      retry_priority_(route_policy.retryPriority()),
      retriable_status_codes_(route_policy.retriableStatusCodes()),
      retriable_headers_(route_policy.retriableHeaders()) {

  std::chrono::milliseconds base_interval(
      runtime_.snapshot().getInteger("upstream.base_retry_backoff_ms", 25));
  if (route_policy.baseInterval()) {
    base_interval = *route_policy.baseInterval();
  }

  // By default, cap the max interval to 10 times the base interval to ensure reasonable back-off
  // intervals.
  std::chrono::milliseconds max_interval = base_interval * 10;
  if (route_policy.maxInterval()) {
    max_interval = *route_policy.maxInterval();
  }

  backoff_strategy_ = std::make_unique<JitteredBackOffStrategy>(base_interval.count(),
                                                                max_interval.count(), random_);
  host_selection_max_attempts_ = route_policy.hostSelectionMaxAttempts();

  // Merge in the headers.
  if (request_headers.EnvoyRetryOn()) {
    retry_on_ |= parseRetryOn(request_headers.getEnvoyRetryOnValue()).first;
  }
  if (request_headers.EnvoyRetryGrpcOn()) {
    retry_on_ |= parseRetryGrpcOn(request_headers.getEnvoyRetryGrpcOnValue()).first;
  }

  const auto& retriable_request_headers = route_policy.retriableRequestHeaders();
  if (!retriable_request_headers.empty()) {
    // If this route limits retries by request headers, make sure there is a match.
    bool request_header_match = false;
    for (const auto& retriable_header : retriable_request_headers) {
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
    if (absl::SimpleAtoi(request_headers.getEnvoyMaxRetriesValue(), &temp)) {
      // The max retries header takes precedence if set.
      retries_remaining_ = temp;
    }
  }

  if (request_headers.EnvoyRetriableStatusCodes()) {
    for (const auto code :
         StringUtil::splitToken(request_headers.getEnvoyRetriableStatusCodesValue(), ",")) {
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

RetryStateImpl::~RetryStateImpl() { resetRetry(); }

void RetryStateImpl::enableBackoffTimer() {
  if (!retry_timer_) {
    retry_timer_ = dispatcher_.createTimer([this]() -> void { callback_(); });
  }

  // We use a fully jittered exponential backoff algorithm.
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

std::pair<uint32_t, bool> RetryStateImpl::parseRetryOn(absl::string_view config) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(config, ",", false, true)) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnValues._5xx) {
      ret |= RetryPolicy::RETRY_ON_5XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.GatewayError) {
      ret |= RetryPolicy::RETRY_ON_GATEWAY_ERROR;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.ConnectFailure) {
      ret |= RetryPolicy::RETRY_ON_CONNECT_FAILURE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.EnvoyRateLimited) {
      ret |= RetryPolicy::RETRY_ON_ENVOY_RATE_LIMITED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Retriable4xx) {
      ret |= RetryPolicy::RETRY_ON_RETRIABLE_4XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RefusedStream) {
      ret |= RetryPolicy::RETRY_ON_REFUSED_STREAM;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableStatusCodes) {
      ret |= RetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RetriableHeaders) {
      ret |= RetryPolicy::RETRY_ON_RETRIABLE_HEADERS;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Reset) {
      ret |= RetryPolicy::RETRY_ON_RESET;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

std::pair<uint32_t, bool> RetryStateImpl::parseRetryGrpcOn(absl::string_view retry_grpc_on_header) {
  uint32_t ret = 0;
  bool all_fields_valid = true;
  for (const auto retry_on : StringUtil::splitToken(retry_grpc_on_header, ",", false, true)) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Cancelled) {
      ret |= RetryPolicy::RETRY_ON_GRPC_CANCELLED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.DeadlineExceeded) {
      ret |= RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.ResourceExhausted) {
      ret |= RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Unavailable) {
      ret |= RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Internal) {
      ret |= RetryPolicy::RETRY_ON_GRPC_INTERNAL;
    } else {
      all_fields_valid = false;
    }
  }

  return {ret, all_fields_valid};
}

void RetryStateImpl::resetRetry() {
  if (callback_) {
    cluster_.resourceManager(priority_).retries().dec();
    callback_ = nullptr;
  }
}

RetryStatus RetryStateImpl::shouldRetry(bool would_retry, DoRetryCallback callback) {
  // If a callback is armed from a previous shouldRetry and we don't need to
  // retry this particular request, we can infer that we did a retry earlier
  // and it was successful.
  if (callback_ && !would_retry) {
    cluster_.stats().upstream_rq_retry_success_.inc();
    if (vcluster_) {
      vcluster_->stats().upstream_rq_retry_success_.inc();
    }
  }

  resetRetry();

  if (!would_retry) {
    return RetryStatus::No;
  }

  // The request has exhausted the number of retries allotted to it by the retry policy configured
  // (or the x-envoy-max-retries header).
  if (retries_remaining_ == 0) {
    cluster_.stats().upstream_rq_retry_limit_exceeded_.inc();
    if (vcluster_) {
      vcluster_->stats().upstream_rq_retry_limit_exceeded_.inc();
    }
    return RetryStatus::NoRetryLimitExceeded;
  }

  retries_remaining_--;

  if (!cluster_.resourceManager(priority_).retries().canCreate()) {
    cluster_.stats().upstream_rq_retry_overflow_.inc();
    if (vcluster_) {
      vcluster_->stats().upstream_rq_retry_overflow_.inc();
    }
    return RetryStatus::NoOverflow;
  }

  if (!runtime_.snapshot().featureEnabled("upstream.use_retry", 100)) {
    return RetryStatus::No;
  }

  ASSERT(!callback_);
  callback_ = callback;
  cluster_.resourceManager(priority_).retries().inc();
  cluster_.stats().upstream_rq_retry_.inc();
  if (vcluster_) {
    vcluster_->stats().upstream_rq_retry_.inc();
  }
  enableBackoffTimer();
  return RetryStatus::Yes;
}

RetryStatus RetryStateImpl::shouldRetryHeaders(const Http::ResponseHeaderMap& response_headers,
                                               DoRetryCallback callback) {
  return shouldRetry(wouldRetryFromHeaders(response_headers), callback);
}

RetryStatus RetryStateImpl::shouldRetryReset(Http::StreamResetReason reset_reason,
                                             DoRetryCallback callback) {
  return shouldRetry(wouldRetryFromReset(reset_reason), callback);
}

RetryStatus RetryStateImpl::shouldHedgeRetryPerTryTimeout(DoRetryCallback callback) {
  // A hedged retry on per try timeout is always retried if there are retries
  // left. NOTE: this is a bit different than non-hedged per try timeouts which
  // are only retried if the applicable retry policy specifies either
  // RETRY_ON_5XX or RETRY_ON_GATEWAY_ERROR. This is because these types of
  // retries are associated with a stream reset which is analogous to a gateway
  // error. When hedging on per try timeout is enabled, however, there is no
  // stream reset.
  return shouldRetry(true, callback);
}

bool RetryStateImpl::wouldRetryFromHeaders(const Http::ResponseHeaderMap& response_headers) {
  // A response that contains the x-envoy-ratelimited header comes from an upstream envoy.
  // We retry these only when the envoy-ratelimited policy is in effect.
  if (response_headers.EnvoyRateLimited() != nullptr) {
    return retry_on_ & RetryPolicy::RETRY_ON_ENVOY_RATE_LIMITED;
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_5XX) {
    if (Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_GATEWAY_ERROR) {
    if (Http::CodeUtility::isGatewayError(Http::Utility::getResponseStatus(response_headers))) {
      return true;
    }
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_RETRIABLE_4XX)) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(response_headers));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_RETRIABLE_STATUS_CODES)) {
    for (auto code : retriable_status_codes_) {
      if (Http::Utility::getResponseStatus(response_headers) == code) {
        return true;
      }
    }
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_RETRIABLE_HEADERS) {
    for (const auto& retriable_header : retriable_headers_) {
      if (retriable_header->matchesHeaders(response_headers)) {
        return true;
      }
    }
  }

  if (retry_on_ &
      (RetryPolicy::RETRY_ON_GRPC_CANCELLED | RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
       RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED | RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE |
       RetryPolicy::RETRY_ON_GRPC_INTERNAL)) {
    absl::optional<Grpc::Status::GrpcStatus> status = Grpc::Common::getGrpcStatus(response_headers);
    if (status) {
      if ((status.value() == Grpc::Status::Canceled &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_CANCELLED)) ||
          (status.value() == Grpc::Status::DeadlineExceeded &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED)) ||
          (status.value() == Grpc::Status::ResourceExhausted &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED)) ||
          (status.value() == Grpc::Status::Unavailable &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE)) ||
          (status.value() == Grpc::Status::Internal &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_INTERNAL))) {
        return true;
      }
    }
  }

  return false;
}

bool RetryStateImpl::wouldRetryFromReset(const Http::StreamResetReason reset_reason) {
  // First check "never retry" conditions so we can short circuit (we never
  // retry if the reset reason is overflow).
  if (reset_reason == Http::StreamResetReason::Overflow) {
    return false;
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_RESET) {
    return true;
  }

  if (retry_on_ & (RetryPolicy::RETRY_ON_5XX | RetryPolicy::RETRY_ON_GATEWAY_ERROR)) {
    // Currently we count an upstream reset as a "5xx" (since it will result in
    // one). With RETRY_ON_RESET we may eventually remove these policies.
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_REFUSED_STREAM) &&
      reset_reason == Http::StreamResetReason::RemoteRefusedStreamReset) {
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_CONNECT_FAILURE) &&
      reset_reason == Http::StreamResetReason::ConnectionFailure) {
    return true;
  }

  return false;
}

} // namespace Router
} // namespace Envoy
