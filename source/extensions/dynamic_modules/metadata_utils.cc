#include "source/extensions/dynamic_modules/metadata_utils.h"

#include "source/common/config/metadata.h"

#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace DynamicModules {

bool getDynamicMetadataStringByPath(const envoy::config::core::v3::Metadata& metadata,
                                    envoy_dynamic_module_type_module_buffer filter_name,
                                    envoy_dynamic_module_type_module_buffer path,
                                    envoy_dynamic_module_type_envoy_buffer* result) {
  std::string filter_name_str(filter_name.ptr, filter_name.length);
  std::string path_str(path.ptr, path.length);
  std::vector<std::string> path_parts = absl::StrSplit(path_str, '.');

  const auto& value =
      Envoy::Config::Metadata::metadataValue(&metadata, filter_name_str, path_parts);

  if (value.kind_case() == Protobuf::Value::KIND_NOT_SET) {
    return false;
  }

  // Only string values are supported. Complex types would require serialization
  // to a buffer, but the ABI uses zero-copy pointers to Envoy memory.
  if (value.kind_case() == Protobuf::Value::kStringValue) {
    const auto& str = value.string_value();
    *result = {const_cast<char*>(str.data()), str.size()};
    return true;
  }

  return false;
}

} // namespace DynamicModules
} // namespace Extensions
} // namespace Envoy
