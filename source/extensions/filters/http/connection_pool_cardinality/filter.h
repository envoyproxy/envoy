#pragma once

#include "envoy/common/hashable.h"
#include "envoy/common/random_generator.h"
#include "envoy/http/filter.h"

#include "source/common/common/logger.h"
#include "source/extensions/filters/http/common/pass_through_filter.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ConnectionPoolCardinality {

struct ConnPoolCardinality : public StreamInfo::FilterState::Object, public Envoy::Hashable {
  explicit ConnPoolCardinality(uint32_t i) : idx_(i) {}
  absl::optional<uint64_t> hash() const override { return idx_; }

  const uint32_t idx_;
};

/**
 * A HTTP filter that adds random dynamic state for downstream requests.
 * This enables load balancing requests across multiple connection pools.
 */
class Filter : public Http::PassThroughDecoderFilter, public Logger::Loggable<Logger::Id::misc> {
public:
  Filter(uint32_t connection_pool_count, Random::RandomGenerator& random)
      : connection_pool_count_(connection_pool_count), random_(random) {}
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const uint32_t connection_pool_count_;
  Random::RandomGenerator& random_;
};

} // namespace ConnectionPoolCardinality
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
