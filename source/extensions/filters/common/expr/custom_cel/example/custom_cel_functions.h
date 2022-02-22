#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

// Toy functions for the Example Custom CEL Vocabulary
//
// Either standard functions or CelFunctions can be used.
// The standard functions will be converted to CelFunctions when added to the
// registry and activation.
// All standard functions will need a Protobuf Arena because CelFunction::Evaluate takes
// Arena as a parameter.
//
// Receiver style: If set to true, function calls have the form arg.function instead of
// function(arg)

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace Example {

using google::api::expr::runtime::CelFunction;
using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelValue;

class GetProduct : public CelFunction {
public:
  explicit GetProduct(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64, CelValue::Type::kInt64}}) {}
  explicit GetProduct(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64, CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class GetDouble : public CelFunction {
public:
  explicit GetDouble(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64}}) {}
  explicit GetDouble(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class Get99 : public CelFunction {
public:
  explicit Get99(absl::string_view name) : CelFunction({std::string(name), false, {}}) {}
  explicit Get99(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

CelValue getSquareOf(Protobuf::Arena* arena, int64_t i);

CelValue getNextInt(Protobuf::Arena* arena, int64_t i);

} // namespace Example
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
