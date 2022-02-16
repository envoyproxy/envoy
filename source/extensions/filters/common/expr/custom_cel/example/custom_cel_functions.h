#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

// Toy functions for the Example Custom CEL Vocabulary
//
// Either standard functions or CelFunctions can be used.
// The standard functions will be converted to CelFunctions when added to the
// registry and activation.
// All functions will need a Protobuf arena because CelFunction::Evaluate takes
// arena as a parameter.
//
// Receiver style: If set to true, function calls have the form 4.getSquareOf instead of
// getSquareOf(4)

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_CEL {
namespace Example {

using google::api::expr::runtime::CelFunction;
using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelValue;

class GetProductCELFunction : public CelFunction {
public:
  explicit GetProductCELFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64, CelValue::Type::kInt64}}) {}
  explicit GetProductCELFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64, CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class GetDoubleCELFunction : public CelFunction {
public:
  explicit GetDoubleCELFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64}}) {}
  explicit GetDoubleCELFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class Get99CELFunction : public CelFunction {
public:
  explicit Get99CELFunction(absl::string_view name) : CelFunction({std::string(name), false, {}}) {}
  explicit Get99CELFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

CelValue getSquareOf(Protobuf::Arena* arena, int64_t i);

CelValue getNextInt(Protobuf::Arena* arena, int64_t i);

} // namespace Example
} // namespace Custom_CEL
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
