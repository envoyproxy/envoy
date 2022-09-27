#include "source/extensions/filters/network/thrift_proxy/filters/payload_to_metadata/payload_to_metadata_filter.h"

#include "source/common/common/base64.h"
#include "source/common/common/regex.h"
#include "source/common/network/utility.h"

#include "source/extensions/filters/network/thrift_proxy/router/router_impl.h"

namespace Envoy {
namespace Extensions {
namespace ThriftFilters {
namespace PayloadToMetadataFilter {

using namespace Envoy::Extensions::NetworkFilters;

Config::Config(const envoy::extensions::filters::network::thrift_proxy::filters::
                   payload_to_metadata::v3::PayloadToMetadata& config) {
  UNREFERENCED_PARAMETER(config);
  // for (const auto& entry : config.request_rules()) {
  //   request_rules_.emplace_back(entry);
  // }
}

PayloadToMetadataFilter::PayloadToMetadataFilter(const ConfigSharedPtr config) : config_(config) {}

bool PayloadToMetadataFilter::passthroughSupported() const {
  return true;
}
} // namespace PayloadToMetadataFilter
} // namespace ThriftFilters
} // namespace Extensions
} // namespace Envoy
