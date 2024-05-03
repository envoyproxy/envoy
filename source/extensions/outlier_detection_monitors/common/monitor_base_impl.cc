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

void Monitor::reportResult(const Error& error) {
  // Ignore all results/errors until monitor is reset.
  if (tripped_) {
    return;
  }

  // iterate over all error buckets
  for (auto& bucket : buckets_) {
    // if the bucket is not interested in this type of result/error
    // just ignore it.
    if (!bucket->matchType(error)) {
      continue;
    }

    // check if the bucket "catches" the result.
    if (bucket->match(error)) {
      // Count as error.
      if (onError()) {
        // TODO: this should return bool indicating if this was tripped.
        // if so, call a callback function to post to main thread.
        // ASSERT(false);
        callback_(1);
      }
    } else {
      onSuccess();
    }
  }
}

void processBucketsConfig(
    Monitor& monitor,
    const envoy::extensions::outlier_detection_monitors::common::v3::ErrorBuckets& config) {
  for (const auto& http_bucket : config.http_errors()) {
    monitor.buckets_.push_back(std::make_unique<HTTPErrorCodesBucket>(
        "not-needed", http_bucket.range().start(), http_bucket.range().end()));
  }
}
} // namespace Outlier
} // namespace Extensions
} // namespace Envoy
