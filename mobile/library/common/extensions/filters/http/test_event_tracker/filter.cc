#include "library/common/extensions/filters/http/test_event_tracker/filter.h"

#include "library/common/api/external.h"
#include "library/common/bridge/utility.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

TestEventTrackerFilterConfig::TestEventTrackerFilterConfig(
    const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
        proto_config)
    : event_tracker_(static_cast<envoy_event_tracker*>(
          Api::External::retrieveApi(envoy_event_tracker_api_name))) {
  auto attributes = std::vector<std::pair<std::string, std::string>>();
  for (auto& pair : proto_config.attributes()) {
    attributes.push_back({std::string(pair.first), std::string(pair.second)});
  }
  attributes_ = attributes;
}

Http::FilterHeadersStatus TestEventTrackerFilter::decodeHeaders(Http::RequestHeaderMap&, bool) {
  config_->track(Envoy::Bridge::makeEnvoyMap(config_->attributes()));
  return Http::FilterHeadersStatus::Continue;
}

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
