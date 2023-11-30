#include "source/extensions/access_loggers/common/access_log_base.h"

#include "source/common/http/header_map_impl.h"
#include "source/common/singleton/const_singleton.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Common {

void ImplBase::log(const Formatter::HttpFormatterContext& log_context,
                   const StreamInfo::StreamInfo& stream_info) {

  if (filter_ && !filter_->evaluate(log_context, stream_info)) {
    return;
  }

  return emitLog(log_context, stream_info);
}

} // namespace Common
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
