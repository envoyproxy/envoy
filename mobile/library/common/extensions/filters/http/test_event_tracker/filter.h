#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "library/common/api/c_types.h"
#include "library/common/extensions/filters/http/test_event_tracker/filter.pb.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

class TestEventTrackerFilterConfig {
public:
  TestEventTrackerFilterConfig(
      const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
          proto_config);

  std::vector<std::pair<std::string, std::string>> attributes() { return attributes_; };
  void track(envoy_map event) { event_tracker_->track(event, event_tracker_->context); };

private:
  std::vector<std::pair<std::string, std::string>> attributes_;
  const envoy_event_tracker* event_tracker_;
};

using TestEventTrackerFilterConfigSharedPtr = std::shared_ptr<TestEventTrackerFilterConfig>;

// The filter that emits preconfigured events. It's supposed to be used for
// testing of the event tracking functionality only.
class TestEventTrackerFilter final : public ::Envoy::Http::PassThroughFilter {
public:
  TestEventTrackerFilter(TestEventTrackerFilterConfigSharedPtr config) : config_(config) {}

  // StreamDecoderFilter
  ::Envoy::Http::FilterHeadersStatus decodeHeaders(::Envoy::Http::RequestHeaderMap& headers,
                                                   bool end_stream) override;

private:
  const TestEventTrackerFilterConfigSharedPtr config_;
};

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
