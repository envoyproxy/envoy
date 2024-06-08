#include "source/extensions/filters/http/thrift_to_metadata/filter.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/http/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace ThriftToMetadata {

FilterConfig::FilterConfig(
    const envoy::extensions::filters::http::thrift_to_metadata::v3::ThriftToMetadata& proto_config,
    Stats::Scope& scope) {
  UNREFERENCED_PARAMETER(proto_config);
  UNREFERENCED_PARAMETER(scope);
}

} // namespace ThriftToMetadata
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
