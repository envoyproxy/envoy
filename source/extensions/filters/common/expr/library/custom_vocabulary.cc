#include "source/extensions/filters/common/expr/library/custom_vocabulary.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

absl::optional<CelValue> CustomVocabularyWrapper::operator[](CelValue key) const {
  if (!key.IsString()) {
    return {};
  }
  auto value = key.StringOrDie().value();
  if (value == "team") {
    return CelValue::CreateStringView("swg");
  } else if (value == "ip") {
    auto upstream_local_address = info_.upstreamLocalAddress();
    if (upstream_local_address != nullptr) {
      return CelValue::CreateStringView(upstream_local_address->asStringView());
    }
  }

  return {};
}

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
