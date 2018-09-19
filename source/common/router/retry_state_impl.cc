#include "common/router/retry_state_impl.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "common/common/assert.h"
#include "common/common/utility.h"
#include "common/grpc/common.h"
#include "common/http/codes.h"
#include "common/http/headers.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Router {

// These are defined in envoy/router/router.h, however during certain cases the compiler is
// refusing to use the header version so allocate space here.
const uint32_t RetryPolicy::RETRY_ON_5XX;
const uint32_t RetryPolicy::RETRY_ON_GATEWAY_ERROR;
const uint32_t RetryPolicy::RETRY_ON_CONNECT_FAILURE;
const uint32_t RetryPolicy::RETRY_ON_RETRIABLE_4XX;
const uint32_t RetryPolicy::RETRY_ON_GRPC_CANCELLED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
const uint32_t RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;

RetryStatePtr RetryStateImpl::create(const RetryPolicy& route_policy,
                                     Http::HeaderMap& request_headers,
                                     const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                                     Runtime::RandomGenerator& random,
                                     Event::Dispatcher& dispatcher,
                                     Upstream::ResourcePriority priority) {
  RetryStatePtr ret;

  // We short circuit here and do not both with an allocation if there is no chance we will retry.
  if (request_headers.EnvoyRetryOn() || request_headers.EnvoyRetryGrpcOn() ||
      route_policy.retryOn()) {
    ret.reset(new RetryStateImpl(route_policy, request_headers, cluster, runtime, random,
                                 dispatcher, priority));
  }

  request_headers.removeEnvoyRetryOn();
  request_headers.removeEnvoyRetryGrpcOn();
  request_headers.removeEnvoyMaxRetries();
  return ret;
}

RetryStateImpl::RetryStateImpl(const RetryPolicy& route_policy, Http::HeaderMap& request_headers,
                               const Upstream::ClusterInfo& cluster, Runtime::Loader& runtime,
                               Runtime::RandomGenerator& random, Event::Dispatcher& dispatcher,
                               Upstream::ResourcePriority priority)
    : cluster_(cluster), runtime_(runtime), random_(random), dispatcher_(dispatcher),
      priority_(priority), retry_host_predicates_(route_policy.retryHostPredicates()),
      retry_priority_(route_policy.retryPriority()) {

  if (request_headers.EnvoyRetryOn()) {
    retry_on_ = parseRetryOn(request_headers.EnvoyRetryOn()->value().c_str());
  }
  if (request_headers.EnvoyRetryGrpcOn()) {
    retry_on_ |= parseRetryGrpcOn(request_headers.EnvoyRetryGrpcOn()->value().c_str());
  }
  if (retry_on_ != 0 && request_headers.EnvoyMaxRetries()) {
    const char* max_retries = request_headers.EnvoyMaxRetries()->value().c_str();
    uint64_t temp;
    if (StringUtil::atoul(max_retries, temp)) {
      retries_remaining_ = temp;
    }
  }

  // Merge in the route policy.
  retry_on_ |= route_policy.retryOn();
  retries_remaining_ = std::max(retries_remaining_, route_policy.numRetries());
  const uint32_t base = runtime_.snapshot().getInteger("upstream.base_retry_backoff_ms", 25);
  // Cap the max interval to 10 times the base interval to ensure reasonable backoff intervals.
  backoff_strategy_ = std::make_unique<JitteredBackOffStrategy>(base, base * 10, random_);
  host_selection_max_attempts_ = route_policy.hostSelectionMaxAttempts();
}

RetryStateImpl::~RetryStateImpl() { resetRetry(); }

void RetryStateImpl::enableBackoffTimer() {
  if (!retry_timer_) {
    retry_timer_ = dispatcher_.createTimer([this]() -> void { callback_(); });
  }

  // We use a fully jittered exponential backoff algorithm.
  retry_timer_->enableTimer(std::chrono::milliseconds(backoff_strategy_->nextBackOffMs()));
}

uint32_t RetryStateImpl::parseRetryOn(absl::string_view config) {
  uint32_t ret = 0;
  for (const auto retry_on : StringUtil::splitToken(config, ",")) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnValues._5xx) {
      ret |= RetryPolicy::RETRY_ON_5XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.GatewayError) {
      ret |= RetryPolicy::RETRY_ON_GATEWAY_ERROR;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.ConnectFailure) {
      ret |= RetryPolicy::RETRY_ON_CONNECT_FAILURE;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.Retriable4xx) {
      ret |= RetryPolicy::RETRY_ON_RETRIABLE_4XX;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnValues.RefusedStream) {
      ret |= RetryPolicy::RETRY_ON_REFUSED_STREAM;
    }
  }

  return ret;
}

uint32_t RetryStateImpl::parseRetryGrpcOn(absl::string_view retry_grpc_on_header) {
  uint32_t ret = 0;
  for (const auto retry_on : StringUtil::splitToken(retry_grpc_on_header, ",")) {
    if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Cancelled) {
      ret |= RetryPolicy::RETRY_ON_GRPC_CANCELLED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.DeadlineExceeded) {
      ret |= RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.ResourceExhausted) {
      ret |= RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED;
    } else if (retry_on == Http::Headers::get().EnvoyRetryOnGrpcValues.Unavailable) {
      ret |= RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE;
    }
  }

  return ret;
}

void RetryStateImpl::resetRetry() {
  if (callback_) {
    cluster_.resourceManager(priority_).retries().dec();
    callback_ = nullptr;
  }
}

RetryStatus RetryStateImpl::shouldRetry(const Http::HeaderMap* response_headers,
                                        const absl::optional<Http::StreamResetReason>& reset_reason,
                                        DoRetryCallback callback) {

  ASSERT((response_headers != nullptr) ^ reset_reason.has_value());

  if (callback_ && !wouldRetry(response_headers, reset_reason)) {
    cluster_.stats().upstream_rq_retry_success_.inc();
  }

  resetRetry();

  if (retries_remaining_ == 0) {
    return RetryStatus::No;
  }

  retries_remaining_--;
  if (!wouldRetry(response_headers, reset_reason)) {
    return RetryStatus::No;
  }

  if (!cluster_.resourceManager(priority_).retries().canCreate()) {
    cluster_.stats().upstream_rq_retry_overflow_.inc();
    return RetryStatus::NoOverflow;
  }

  if (!runtime_.snapshot().featureEnabled("upstream.use_retry", 100)) {
    return RetryStatus::No;
  }

  ASSERT(!callback_);
  callback_ = callback;
  cluster_.resourceManager(priority_).retries().inc();
  cluster_.stats().upstream_rq_retry_.inc();
  enableBackoffTimer();
  return RetryStatus::Yes;
}

bool RetryStateImpl::wouldRetry(const Http::HeaderMap* response_headers,
                                const absl::optional<Http::StreamResetReason>& reset_reason) {
  // We never retry if the overloaded header is set.
  if (response_headers != nullptr && response_headers->EnvoyOverloaded() != nullptr) {
    return false;
  }

  // we never retry if the reset reason is overflow.
  if (reset_reason && reset_reason.value() == Http::StreamResetReason::Overflow) {
    return false;
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_5XX) {
    // wouldRetry() is passed null headers when there was an upstream reset. Currently we count an
    // upstream reset as a "5xx" (since it will result in one). We may eventually split this out
    // into its own type. I.e., RETRY_ON_RESET.
    if (!response_headers ||
        Http::CodeUtility::is5xx(Http::Utility::getResponseStatus(*response_headers))) {
      return true;
    }
  }

  if (retry_on_ & RetryPolicy::RETRY_ON_GATEWAY_ERROR) {
    if (!response_headers ||
        Http::CodeUtility::isGatewayError(Http::Utility::getResponseStatus(*response_headers))) {
      return true;
    }
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_REFUSED_STREAM) && reset_reason &&
      reset_reason.value() == Http::StreamResetReason::RemoteRefusedStreamReset) {
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_CONNECT_FAILURE) && reset_reason &&
      reset_reason.value() == Http::StreamResetReason::ConnectionFailure) {
    return true;
  }

  if ((retry_on_ & RetryPolicy::RETRY_ON_RETRIABLE_4XX) && response_headers) {
    Http::Code code = static_cast<Http::Code>(Http::Utility::getResponseStatus(*response_headers));
    if (code == Http::Code::Conflict) {
      return true;
    }
  }

  if (retry_on_ &
          (RetryPolicy::RETRY_ON_GRPC_CANCELLED | RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED |
           RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED |
           RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE) &&
      response_headers) {
    absl::optional<Grpc::Status::GrpcStatus> status =
        Grpc::Common::getGrpcStatus(*response_headers);
    if (status) {
      if ((status.value() == Grpc::Status::Canceled &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_CANCELLED)) ||
          (status.value() == Grpc::Status::DeadlineExceeded &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_DEADLINE_EXCEEDED)) ||
          (status.value() == Grpc::Status::ResourceExhausted &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_RESOURCE_EXHAUSTED)) ||
          (status.value() == Grpc::Status::Unavailable &&
           (retry_on_ & RetryPolicy::RETRY_ON_GRPC_UNAVAILABLE))) {
        return true;
      }
    }
  }

  return false;
}

} // namespace Router
} // namespace Envoy
