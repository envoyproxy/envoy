#include "source/extensions/access_loggers/fluentd/substitution_formatter.h"

#include "envoy/stream_info/stream_info.h"

#include "source/common/json/json_loader.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

FluentdFormatterImpl::FluentdFormatterImpl(Formatter::FormatterPtr json_formatter)
    : json_formatter_(std::move(json_formatter)) {}

std::vector<uint8_t> FluentdFormatterImpl::format(const Formatter::HttpFormatterContext& context,
                                                  const StreamInfo::StreamInfo& stream_info) const {
  auto json_string = json_formatter_->formatWithContext(context, stream_info);
  return Json::Factory::jsonToMsgpack(json_string);
}

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
