#pragma once

#include "envoy/access_log/access_log.h"
#include "envoy/http/header_map.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/access_log/access_log_impl.h"
#include "source/common/config/utility.h"
#include "source/common/protobuf/message_validator_impl.h"
#include "source/common/protobuf/protobuf.h"
#include "source/common/protobuf/utility.h"
#include "source/extensions/filters/common/expr/evaluator.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Filters {
namespace CEL {

class CELAccessLogExtensionFilter : public AccessLog::Filter {
public:
  CELAccessLogExtensionFilter(const ::Envoy::LocalInfo::LocalInfo& local_info,
                              Extensions::Filters::Common::Expr::BuilderInstanceSharedConstPtr,
                              const cel::expr::Expr&);

  bool evaluate(const Formatter::Context& log_context,
                const StreamInfo::StreamInfo& stream_info) const override;

private:
  const ::Envoy::LocalInfo::LocalInfo& local_info_;
  const Extensions::Filters::Common::Expr::CompiledExpression expr_;
};

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
