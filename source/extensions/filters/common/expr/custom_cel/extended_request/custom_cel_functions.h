#pragma once

#include "envoy/http/header_map.h"

#include "source/common/http/headers.h"
#include "source/common/http/utility.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/custom_cel/extended_request/utility/utility.h"

#include "eval/public/cel_function.h"
#include "eval/public/cel_value.h"
#include "eval/public/containers/container_backed_map_impl.h"

// Functions for the Extended Request CEL Vocabulary
//
// Either standard functions or CelFunctions can be used.
// The standard functions will be converted to CelFunctions when added
// to the registry and activation. All standard functions will need a Protobuf Arena because
// CelFunction::Evaluate takes Arena as a parameter. Any other parameters must be of type CelValue.
// Non-CelValue parameters can be added via lambda captures.
//
// Receiver style: If set to true, function calls have the form arg.function instead of
// function(arg)

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {

// static/stateless function
class UrlFunction : public ::google::api::expr::runtime::CelFunction {
public:
  explicit UrlFunction(absl::string_view name)
      : ::google::api::expr::runtime::CelFunction(
            {std::string(name), true, {google::api::expr::runtime::CelValue::Type::kMap}}) {}

  absl::Status Evaluate(absl::Span<const google::api::expr::runtime::CelValue> args,
                        google::api::expr::runtime::CelValue* output,
                        Protobuf::Arena* arena) const override;
};

// lazy/stateful functions

// cookie: Get cookies as a map.
google::api::expr::runtime::CelValue cookie(Protobuf::Arena* arena,
                                            const Http::RequestHeaderMap& request_header_map);

// cookieValue: given a cookie name, get its value
google::api::expr::runtime::CelValue cookieValue(Protobuf::Arena* arena,
                                                 const Http::RequestHeaderMap& request_header_map,
                                                 google::api::expr::runtime::CelValue key);

} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
