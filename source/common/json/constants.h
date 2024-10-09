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

  // Constants for common JSON delimiters.
  static constexpr absl::string_view DoubleQuote = "\"";
  static constexpr absl::string_view Comma = ",";
  static constexpr absl::string_view Colon = ":";
  static constexpr absl::string_view ArrayBegin = "[";
  static constexpr absl::string_view ArrayEnd = "]";
  static constexpr absl::string_view MapBegin = "{";
  static constexpr absl::string_view MapEnd = "}";
};

} // namespace Json
} // namespace Envoy
