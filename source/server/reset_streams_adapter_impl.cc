#include "source/server/reset_streams_adapter_impl.h"

namespace Envoy {
namespace Server {
namespace {

// The number of buckets the Watermark Buffer Factory has.
constexpr int kNumBuckets = 8;
} // namespace

ResetStreamAdapterImpl::ResetStreamAdapterImpl(double lower_limit, double upper_limit)
    : lower_limit_(lower_limit), upper_limit_(upper_limit),
      bucket_gradation_((upper_limit_ - lower_limit_) / kNumBuckets) {
  // Upper limit should be > Lower limit
  ASSERT(upper_limit_ != lower_limit_);
}

absl::optional<uint32_t>
ResetStreamAdapterImpl::translateToBucketsToReset(OverloadActionState state) const {
  // Scale from [0, 1] to [0, 100].
  const double current_pressure = state.value().value() * 100;

  // Check extremities.
  if (current_pressure < lower_limit_) {
    return {}; // consider changing to optional
  }
  if (current_pressure >= upper_limit_) {
    // Reset all buckets
    return 0;
  }

  // Calculate within [lower_limit_, upper_limit_).
  const int buckets_to_clear =
      std::floor((current_pressure - lower_limit_) / bucket_gradation_) + 1;

  return kNumBuckets - buckets_to_clear;
}

} // namespace Server
} // namespace Envoy
