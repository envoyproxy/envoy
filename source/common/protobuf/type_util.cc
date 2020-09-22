#include "common/protobuf/type_util.h"

namespace Envoy {

absl::string_view TypeUtil::typeUrlToDescriptorFullName(absl::string_view type_url) {
  const size_t pos = type_url.rfind('/');
  if (pos != absl::string_view::npos) {
    type_url = type_url.substr(pos + 1);
  }
  return type_url;
}

std::string TypeUtil::descriptorFullNameToTypeUrl(absl::string_view type) {
  return "type.googleapis.com/" + std::string(type);
}

} // namespace Envoy
