#include "source/extensions/filters/http/proto_api_scrubber/filter_config.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"

#include "absl/status/status.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ProtoApiScrubber {
namespace {

using ::envoy::extensions::filters::http::proto_api_scrubber::v3::ProtoApiScrubberConfig;
} // namespace

FilterConfig::FilterConfig(const ProtoApiScrubberConfig&) {}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
