#include "source/extensions/filters/common/expr/library/custom_functions.h"

#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

absl::Status GetDoubleCelFunction::Evaluate(absl::Span<const CelValue> args, CelValue* output,
                                            Protobuf::Arena* arena) const {
  // using arena so that it will not be unused
  arena->SpaceUsed();
  if (args[0].type() == CelValue::Type::kInt64) {
    int64_t value = 2 * args[0].Int64OrDie();
    *output = CelValue::CreateInt64(value);
    return absl::OkStatus();
  }
  *output = CelValue::CreateInt64(-1);
  return absl::InvalidArgumentError("expected int argument for function GetDouble");
}

CelValue GetNextInt(Protobuf::Arena* arena, int64_t i) {
  // using arena so that it will not be unused
  arena->SpaceUsed();
  return CelValue::CreateInt64(i+1);
}

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
