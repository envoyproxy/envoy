#pragma once

#include "common/protobuf/protobuf.h"

#include "absl/strings/string_view.h"
#include "absl/types/optional.h"

namespace Envoy {

class TypeUtil {
public:
  static absl::string_view typeUrlToDescriptorFullName(absl::string_view type_url);

  static std::string descriptorFullNameToTypeUrl(absl::string_view type);
};

} // namespace Envoy
