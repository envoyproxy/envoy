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

class GetProductCelFunction : public CelFunction {
public:
  explicit GetProductCelFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64, CelValue::Type::kInt64}}) {}
  explicit GetProductCelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor CreateDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64, CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class GetDoubleCelFunction : public CelFunction {
public:
  explicit GetDoubleCelFunction(absl::string_view name)
      : CelFunction({std::string(name), false, {CelValue::Type::kInt64}}) {}
  explicit GetDoubleCelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor CreateDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {CelValue::Type::kInt64}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

class Get99CelFunction : public CelFunction {
public:
  explicit Get99CelFunction(absl::string_view name) : CelFunction({std::string(name), false, {}}) {}
  explicit Get99CelFunction(const CelFunctionDescriptor& desc) : CelFunction(desc) {}

  static CelFunctionDescriptor CreateDescriptor(absl::string_view name) {
    return CelFunctionDescriptor{name, false, {}};
  }

  absl::Status Evaluate(absl::Span<const CelValue> args, CelValue* output,
                        Protobuf::Arena* arena) const override;
};

CelValue GetNextInt(Protobuf::Arena* arena, int64_t i);

} // namespace Library
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
