#include "source/extensions/filters/common/expr/library/custom_vocabulary.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"

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
  } else if (value == "protocol") {
    if (info_.protocol().has_value()) {
      // creating string in this manner in order to use arena_
      return CelValue::CreateString(
          Protobuf::Arena::Create<std::string>(&arena_,
                                               Http::Utility::getProtocolString(
                                                   info_.protocol().value())));
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
