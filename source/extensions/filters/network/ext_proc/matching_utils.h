#pragma once

#include "envoy/local_info/local_info.h"

#include "source/common/common/logger.h"
#include "source/common/protobuf/protobuf.h"
#include "source/extensions/filters/common/expr/evaluator.h"

#include "absl/container/flat_hash_map.h"
#include "absl/status/status.h"
#include "cel/expr/syntax.pb.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace ExtProc {

class ExpressionManager : public Logger::Loggable<Logger::Id::ext_proc> {
  friend class ExpressionManagerTestAccessor;

public:
  using CelExpression = Filters::Common::Expr::CompiledExpression;

  ExpressionManager(Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder,
                    const LocalInfo::LocalInfo* local_info,
                    const Protobuf::RepeatedPtrField<std::string>& connection_attributes,
                    absl::Status& creation_status)
      : builder_(builder), local_info_(local_info),
        connection_expr_(initExpressions(connection_attributes, creation_status)) {}

  bool hasConnectionExpr() const { return !connection_expr_.empty(); }

  Protobuf::Struct
  evaluateConnectionAttributes(const Filters::Common::Expr::Activation& activation) const {
    return evaluateAttributes(activation, connection_expr_);
  }

  static Protobuf::Struct
  evaluateAttributes(const Filters::Common::Expr::Activation& activation,
                     const absl::flat_hash_map<std::string, CelExpression>& expr);

  const LocalInfo::LocalInfo* localInfo() const { return local_info_; }

private:
  absl::flat_hash_map<std::string, CelExpression>
  initExpressions(const Protobuf::RepeatedPtrField<std::string>& matchers,
                  absl::Status& creation_status);

  const Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr builder_;
  const LocalInfo::LocalInfo* local_info_{nullptr};
  const absl::flat_hash_map<std::string, CelExpression> connection_expr_;
};

} // namespace ExtProc
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
