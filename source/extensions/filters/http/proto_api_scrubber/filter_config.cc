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

FilterConfig::FilterConfig(const ProtoApiScrubberConfig& proto_config)
    : proto_config_(proto_config) {}

void FilterConfig::PrintConfig() {
  std::string json_config_str;
  absl::Status status = Protobuf::util::MessageToJsonString(proto_config_, &json_config_str);
  if (status.ok()) {
    ENVOY_LOG(debug, "ProtoApiScrubber config initialized with {}", json_config_str);
  } else {
    ENVOY_LOG(debug, "ProtoApiScrubber config to json conversion failed.");
  }
}

} // namespace ProtoApiScrubber
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
