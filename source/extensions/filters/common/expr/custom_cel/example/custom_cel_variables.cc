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

absl::optional<CelValue> CustomWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Team) {
    return CelValue::CreateStringView("spirit");
  } else if (value == Protocol) {
    if (info_.protocol().has_value()) {
      return CelValue::CreateString(Protobuf::Arena::Create<std::string>(
          &arena_, Http::Utility::getProtocolString(info_.protocol().value())));
    }
  }

  return {};
}

absl::optional<CelValue> SourceWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == Address) {
    return CelValue::CreateStringView(
        info_.downstreamAddressProvider().remoteAddress()->asStringView());
  } else if (value == Port) {
    if (info_.downstreamAddressProvider().remoteAddress()->ip() != nullptr) {
      return CelValue::CreateInt64(info_.downstreamAddressProvider().remoteAddress()->ip()->port());
    }
  } else if (value == Description) {
    return CelValue::CreateString(
        Protobuf::Arena::Create<std::string>(&arena_, "description: has address, port values"));
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
