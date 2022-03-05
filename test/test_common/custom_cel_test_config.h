#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {
namespace CustomCel {
namespace ExtendedRequest {
namespace TestConfig {

constexpr absl::string_view QueryExpr = R"EOF(
           call_expr:
             function: contains
             args:
             - select_expr:
                 operand:
                   select_expr:
                     operand:
                       ident_expr:
                         name: request
                     field: query
                 field: key1
             - const_expr:
                 string_value: {}
)EOF";

constexpr absl::string_view CookieExpr = R"EOF(
            call_expr:
              function: contains
              args:
              - call_expr:
                  function: _[_]
                  args:
                  - call_expr:
                      function: cookie
                  - const_expr:
                      string_value: fruit
              - const_expr:
                  string_value: {}
)EOF";

constexpr absl::string_view CookieValueExpr = R"EOF(
             call_expr:
               function: contains
               args:
               - call_expr:
                   function: cookieValue
                   args:
                   - const_expr:
                       string_value: fruit
               - const_expr:
                   string_value: {}
)EOF";

constexpr absl::string_view UrlExpr = R"EOF(
             call_expr:
               function: contains
               args:
               - call_expr:
                   target:
                     ident_expr:
                       name: request
                   function: url
               - const_expr:
                   string_value: {}
)EOF";

} // namespace TestConfig
} // namespace ExtendedRequest
} // namespace CustomCel
} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
