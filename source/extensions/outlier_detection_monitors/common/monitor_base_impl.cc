#include "source/extensions/outlier_detection_monitors/common/monitor_base_impl.h"

namespace Envoy {
namespace Extensions {
namespace Outlier {

bool HTTPCodesBucket::match(const ExtResult& result) const {
  // We should not get here with errors other then HTTP codes.
  ASSERT(matchType(result));
  const HttpCode http_code = absl::get<HttpCode>(result);
  return ((http_code.code() >= start_) && (http_code.code() <= end_));
}

bool LocalOriginEventsBucket::match(const ExtResult& event) const {
  // We should not get here with errors other then Local Origin events.
  ASSERT(matchType(event));
  const LocalOriginEvent local_origin_event = absl::get<LocalOriginEvent>(event);
  // Capture all events except the success
  return (!((local_origin_event.result() == Result::LocalOriginConnectSuccessFinal) ||
            (local_origin_event.result() == Result::ExtOriginRequestSuccess)));
}

void ExtMonitorBase::putResult(const ExtResult result) {
  if (config_->buckets().empty()) {
    return;
  }

  bool matched_error = false;
  bool matched_type = false;
  // iterate over all error buckets
  for (auto& bucket : config_->buckets()) {
    // if the bucket is not interested in this type of result/error
    // just ignore it.
    if (!bucket->matchType(result)) {
      continue;
    }

    matched_type = true;

    // check if the bucket "catches" the result.
    if (bucket->match(result)) {
      matched_error = true;
      break;
    }
  }

  // If none of buckets had the matching type, just bail out.
  if (!matched_type) {
    return;
  }

  if (matched_error) {
    // Count as error.
    if (onError()) {
      callback_(this);
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

void ExtMonitorConfig::processBucketsConfig(
    const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config) {
  for (const auto& http_bucket : config.http_errors()) {
    addErrorBucket(
        std::make_unique<HTTPCodesBucket>(http_bucket.range().start(), http_bucket.range().end()));
  }
  for (auto i = 0; i < config.local_origin_errors().size(); i++) {
    addErrorBucket(std::make_unique<LocalOriginEventsBucket>());
  }
}
} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
