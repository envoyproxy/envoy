#include "source/extensions/tracers/zipkin/util.h"

#include "source/common/common/utility.h"

#include "absl/strings/str_cat.h"

namespace Envoy {
namespace Extensions {
namespace Tracers {
namespace Zipkin {

Protobuf::Value Util::uint64Value(uint64_t value, absl::string_view name,
                                  Replacements& replacements) {
  const std::string string_value = std::to_string(value);
  replacements.push_back({absl::StrCat("\"", name, "\":\"", string_value, "\""),
                          absl::StrCat("\"", name, "\":", string_value)});
  return ValueUtil::stringValue(string_value);
}

} // namespace Zipkin
} // namespace Tracers
} // namespace Extensions
} // namespace Envoy
