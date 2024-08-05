#pragma once

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace Expr {

// random()
inline constexpr char SampleCelString[] = R"pb(
  expr: {
    id: 2
    call_expr: {
      target: {
        id: 1
        ident_expr: {
          name: "context"
        }
      }
      function: "sample"
      args: {
        id: 3
        const_expr: {
          double_value: 0.5
        }
      }
      args: {
        id: 4
        const_expr: {
          int64_value: 1
        }
      }
    }
  }
)pb";

} // namespace Expr
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
