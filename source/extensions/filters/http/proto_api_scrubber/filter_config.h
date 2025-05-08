#pragma once

#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.h"
#include "envoy/extensions/filters/http/proto_api_scrubber/v3/config.pb.validate.h"

#include "source/common/common/logger.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {

// The config for Proto API Scrubber filter. As a thread-safe class, it
// should be constructed only once and shared among filters for better
// performance.
class FilterConfig : public Envoy::Logger::Loggable<Envoy::Logger::Id::filter> {
public:
  explicit FilterConfig(
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
          proto_config);
  // This method would be used for debugging purpose only, during development.
  // It would be removed once the development is complete.
  void PrintConfig();

private:
  const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&
      proto_config_;
};

using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
