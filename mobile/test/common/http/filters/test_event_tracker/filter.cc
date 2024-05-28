#include "test/common/http/filters/test_event_tracker/filter.h"

#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

TestEventTrackerFilterConfig::TestEventTrackerFilterConfig(
    const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
        proto_config)
    : event_tracker_(static_cast<std::unique_ptr<EnvoyEventTracker>*>(
          Api::External::retrieveApi(ENVOY_EVENT_TRACKER_API_NAME))) {
  for (auto& [key, value] : proto_config.attributes()) {
    attributes_.emplace(std::string(key), std::string(value));
  }
}

Http::FilterHeadersStatus TestEventTrackerFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  config_->track(config_->attributes());
  return Http::FilterHeadersStatus::Continue;
}

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
