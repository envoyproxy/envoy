#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_variables.h"

#include "source/common/http/utility.h"
#include "source/extensions/filters/common/expr/context.h"

#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

absl::optional<CelValue> CustomCelVariablesWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == "team") {
    return CelValue::CreateStringView("spirit");
  } else if (value == "protocol") {
    if (info_.protocol().has_value()) {
      return CelValue::CreateString(Protobuf::Arena::Create<std::string>(
          &arena_, Http::Utility::getProtocolString(info_.protocol().value())));
    }
  }

  return {};
}

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
