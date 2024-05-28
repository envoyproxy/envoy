#pragma once

#include "envoy/http/filter.h"

#include "source/extensions/filters/http/common/pass_through_filter.h"

#include "test/common/http/filters/test_event_tracker/filter.pb.h"

#include "library/common/api/c_types.h"
#include "library/common/engine_types.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace TestEventTracker {

class TestEventTrackerFilterConfig {
public:
  TestEventTrackerFilterConfig(
      const envoymobile::extensions::filters::http::test_event_tracker::TestEventTracker&
          proto_config);

  absl::flat_hash_map<std::string, std::string> attributes() { return attributes_; };
  void track(const absl::flat_hash_map<std::string, std::string>& events) {
    if (event_tracker_ != nullptr) {
      (*event_tracker_)->on_track_(events);
    }
  }

private:
  absl::flat_hash_map<std::string, std::string> attributes_;
  const std::unique_ptr<EnvoyEventTracker>* event_tracker_;
};

using TestEventTrackerFilterConfigSharedPtr = std::shared_ptr<TestEventTrackerFilterConfig>;

// The filter that emits preconfigured events. It's supposed to be used for
// testing of the event tracking functionality only.
class TestEventTrackerFilter final : public Http::PassThroughFilter {
public:
  TestEventTrackerFilter(TestEventTrackerFilterConfigSharedPtr config) : config_(config) {}

  // StreamDecoderFilter
  Http::FilterHeadersStatus decodeHeaders(Http::RequestHeaderMap& headers,
                                          bool end_stream) override;

private:
  const TestEventTrackerFilterConfigSharedPtr config_;
};

} // namespace TestEventTracker
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
