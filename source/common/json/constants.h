#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Json {

class Constants {
public:
  // Constants for common JSON values.
  static constexpr absl::string_view True = "true";
  static constexpr absl::string_view False = "false";
  static constexpr absl::string_view Null = "null";
};

} // namespace Json
} // namespace Envoy
