#include "source/extensions/filters/common/expr/custom_cel/example/custom_cel_functions.h"

#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace Example {

absl::Status GetProduct::Evaluate(absl::Span<const CelValue> args, CelValue* output,
                                  Protobuf::Arena*) const {
  int64_t value = args[0].Int64OrDie() * args[1].Int64OrDie();
  *output = CelValue::CreateInt64(value);
  return absl::OkStatus();
}

absl::Status GetDouble::Evaluate(absl::Span<const CelValue> args, CelValue* output,
                                 Protobuf::Arena*) const {
  int64_t value = 2 * args[0].Int64OrDie();
  *output = CelValue::CreateInt64(value);
  return absl::OkStatus();
}

absl::Status Get99::Evaluate(absl::Span<const CelValue>, CelValue* output, Protobuf::Arena*) const {
  *output = CelValue::CreateInt64(99);
  return absl::OkStatus();
}

CelValue getSquareOf(Protobuf::Arena*, int64_t i) { return CelValue::CreateInt64(i * i); }

CelValue getNextInt(Protobuf::Arena*, int64_t i) { return CelValue::CreateInt64(i + 1); }

} // namespace Example
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
