#pragma once

#include "source/common/protobuf/protobuf.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"

// Toy functions for the example custom cel vocabulary

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace Custom_Cel {
namespace Example {

using google::api::expr::runtime::CelFunction;
using google::api::expr::runtime::CelFunctionDescriptor;
using google::api::expr::runtime::CelValue;

class getProductCelFunction : public CelFunction {
public:
  explicit getProductCelFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64, CelValue::Type::kInt64}}) {}
  explicit getProductCelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64, CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class getDoubleCelFunction : public CelFunction {
public:
  explicit getDoubleCelFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64}}) {}
  explicit getDoubleCelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class get99CelFunction : public CelFunction {
public:
  explicit get99CelFunction(absl::string_view name) : CelFunction({std::string(name), false, {}}) {}
  explicit get99CelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor createDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

CelValue getSquareOf(Protobuf::Arena* arena, int64_t i);

CelValue getNextInt(Protobuf::Arena* arena, int64_t i);

} // namespace Example
} // namespace Custom_Cel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
