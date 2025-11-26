#include "source/extensions/filters/http/connection_pool_cardinality/filter.h"

#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

Http::FilterHeadersStatus Filter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  if (connection_pool_count_ <= 1) {
    // No dynamic state needed if only one connection pool
    return Http::FilterHeadersStatus::Continue;
  }

  const uint32_t pool_index = random_.random() % connection_pool_count_;

  // Set dynamic metadata for connection pool selection
  auto filter_state = decoder_callbacks_->streamInfo().filterState();
  filter_state->setData(
      "connection_pool_cardinality", std::make_shared<ConnPoolCardinality>(pool_index),
      StreamInfo::FilterState::StateType::ReadOnly, StreamInfo::FilterState::LifeSpan::Request,
      StreamInfo::StreamSharingMayImpactPooling::SharedWithUpstreamConnection);

  ENVOY_STREAM_LOG(trace, "Selected connection pool index: {}", *decoder_callbacks_, pool_index);

  return Http::FilterHeadersStatus::Continue;
}

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
