#include "source/extensions/filters/http/rate_limit_quota/filter.h"

// Needed for bucket id, temporary.
#include "envoy/service/rate_limit_quota/v3/rlqs.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace RateLimitQuota {

using BucketId = envoy::service::rate_limit_quota::v3::BucketId;
// TODO(tyxia) Think about moving this to indepenet file. No need for a new class at this moment.
namespace RateLimit {
// TODO(tyxia) buildBuckets might be the part of filter rather than the client.
// * the data plane sends a usage report for requests matched into the bucket with ``BucketId``
//   to the control plane
// * the control plane sends an assignment for the bucket with ``BucketId`` to the data plane
//   Bucket ID.
BucketId buildBucketsId(
    const Http::RequestHeaderMap& header,
    const envoy::extensions::filters::http::rate_limit_quota::v3::RateLimitQuotaBucketSettings&
        settings) {
  auto builder = settings.bucket_id_builder().bucket_id_builder();
  BucketId bucket_id;
  for (auto it : builder) {
    auto builder_method = it.second;
    // TODO(tyxia) Use switch case
    if (builder_method.has_string_value()) {
      std::string bucket_id_key = it.first;
      bucket_id.mutable_bucket()->insert({bucket_id_key, builder_method.string_value()});
    } else {
      envoy::type::matcher::v3::HttpRequestHeaderMatchInput input;
      builder_method.custom_value().typed_config().UnpackTo(&input);
      if (input.header_name() == "environment") {
        std::string bucket_id_key = "env";
        // TODO(tyxia) how to find the value from reuqest headers ???
      }
      //std::string header_name = builder_method.custom_value().typed_config().header_name();
      //bucket_id.bucket().insert({bucket_id_key, builder_method.string_value()});
    }
  }

  return bucket_id;
}

// TODO(tyxia) CEL expression.
bool bucketMatcher() {
  return true;
}

/*Initially all Envoy's quota assignments are empty. The rate limit quota filter requests quota
   assignment from RLQS when the request matches a bucket for the first time. The behavior of the
   filter while it waits for the initial assignment is determined by the no_assignment_behavior
   value.
*/

} // namespace RateLimit

Http::FilterHeadersStatus RateLimitQuotaFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  // TODO(tyxia) Perform the CEL matching on
  // And can move it to initical_call
  // buildBuckets(headrs, *config_);

  if (rate_limit_client_->startStream() == true) {
    rate_limit_client_->rateLimit();
    return Envoy::Http::FilterHeadersStatus::StopAllIterationAndWatermark;
  }

  return Envoy::Http::FilterHeadersStatus::Continue;
}

void RateLimitQuotaFilter::onDestroy() { rate_limit_client_->closeStream(); }

} // namespace RateLimitQuota
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
