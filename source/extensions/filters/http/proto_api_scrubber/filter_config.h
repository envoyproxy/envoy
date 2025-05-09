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
      const envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig&);
};

using FilterConfigSharedPtr = std::shared_ptr<const FilterConfig>;

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
