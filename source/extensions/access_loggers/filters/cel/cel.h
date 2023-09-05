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
  CELAccessLogExtensionFilter(Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr,
                              const google::api::expr::v1alpha1::Expr&);

  bool evaluate(const StreamInfo::StreamInfo& info, const Http::RequestHeaderMap& request_headers,
                const Http::ResponseHeaderMap& response_headers,
                const Http::ResponseTrailerMap& response_trailers,
                AccessLog::AccessLogType access_log_type) const override;

private:
  Extensions::Filters::Common::Expr::BuilderInstanceSharedPtr builder_;
  const google::api::expr::v1alpha1::Expr parsed_expr_;
  Extensions::Filters::Common::Expr::ExpressionPtr compiled_expr_;
};

} // namespace CEL
} // namespace Filters
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
