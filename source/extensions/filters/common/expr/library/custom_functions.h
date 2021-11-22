#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Library {

using google::api::expr::runtime::CelFunction;
using google::api::expr::runtime::CelFunctionDescriptor;
using CelValue = google::api::expr::runtime::CelValue;

// Simple function that takes no args and returns an int64.
class ConstCelFunction : public CelFunction {
public:
  explicit ConstCelFunction(absl::string_view name) : CelFunction({std::string(name), false, {}}) {}
  explicit ConstCelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor CreateDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

CelValue GetConstValue(Protobuf::Arena* arena, int64_t i);

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
