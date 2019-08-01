#include "extensions/filters/http/adaptive_concurrency/adaptive_concurrency_filter.h"

#include <chrono>
#include <cstdint>
#include <string>
#include <vector>

#include "extensions/filters/http/adaptive_concurrency/concurrency_controller/concurrency_controller.h"
#include "extensions/filters/http/well_known_names.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AdaptiveConcurrency {

AdaptiveConcurrencyFilterConfig::AdaptiveConcurrencyFilterConfig(
    const envoy::config::filter::http::adaptive_concurrency::v2alpha::AdaptiveConcurrency&,
    Runtime::Loader& runtime, const std::string& stats_prefix, Stats::Scope& scope,
    TimeSource& time_source)
    : runtime_(runtime), stats_prefix_(stats_prefix), scope_(scope), time_source_(time_source) {}

AdaptiveConcurrencyFilter::AdaptiveConcurrencyFilter(
    AdaptiveConcurrencyFilterConfigSharedPtr config, ConcurrencyControllerSharedPtr controller)
    : config_(config), controller_(controller) {}

AdaptiveConcurrencyFilter::~AdaptiveConcurrencyFilter() {}

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::decodeHeaders(Http::HeaderMap&, bool) {
  // TODO (tonya11en).
  return Http::FilterHeadersStatus::Continue;
}

void AdaptiveConcurrencyFilter::onDestroy() {
  // TODO (tonya11en).
}

Http::FilterHeadersStatus AdaptiveConcurrencyFilter::encodeHeaders(Http::HeaderMap&,
                                                                   bool /*end_stream*/) {
  // TODO (tonya11en).
  return Http::FilterHeadersStatus::Continue;
}

} // namespace AdaptiveConcurrency
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
