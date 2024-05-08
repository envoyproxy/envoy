#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

bool HTTPErrorCodesBucket::matches(
    const TypedError<Upstream::Outlier::ErrorType::HTTP_CODE>& error) const {
  // We should not get here with errors other then HTTP codes.
  ASSERT(error.type() == ErrorType::HTTP_CODE);
  const HttpCode& http_code = dynamic_cast<const HttpCode&>(error);
  return ((http_code.code() >= start_) && (http_code.code() <= end_));
}

bool LocalOriginEventsBucket::matches(
    const TypedError<Upstream::Outlier::ErrorType::LOCAL_ORIGIN>& event) const {
  // We should not get here with errors other then HTTP codes.
  ASSERT(event.type() == ErrorType::LOCAL_ORIGIN);
  const LocalOriginEvent& local_origin_event = dynamic_cast<const LocalOriginEvent&>(event);
  // Capture all events except the success
  return (!((local_origin_event.result() == Result::LocalOriginConnectSuccessFinal) ||
            (local_origin_event.result() == Result::ExtOriginRequestSuccess)));
}

void Monitor::reportResult(const Error& error) {
  // Ignore all results/errors until monitor is reset.
  if (tripped_) {
    return;
  }

  if (buckets_.empty()) {
    return;
  }

  bool matched = false;
  // iterate over all error buckets
  for (auto& bucket : buckets_) {
    // if the bucket is not interested in this type of result/error
    // just ignore it.
    if (!bucket->matchType(error)) {
      continue;
    }

    // check if the bucket "catches" the result.
    if (bucket->match(error)) {
      matched = true;
      break;
    }
  }

  if (matched) {
    // Count as error.
    if (onError()) {
      callback_(enforce_, name(), absl::nullopt);
      // Reaching error was reported via callback.
      // but the host may or may not be ejected based on enforce_ parameter.
      // Reset the monitor's state, so a single new error does not
      // immediately trigger error condition again.
      onReset();
    }
  } else {
    onSuccess();
  }
}

void processBucketsConfig(
    Monitor& monitor,
    const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config) {
  for (const auto& http_bucket : config.http_errors()) {
    monitor.buckets_.push_back(std::make_unique<HTTPErrorCodesBucket>(
        "not-needed", http_bucket.range().start(), http_bucket.range().end()));
  }
  if (config.local_origin_errors().size() > 0) {
    monitor.buckets_.push_back(std::make_unique<LocalOriginEventsBucket>("not-needed"));
  }
}
} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
