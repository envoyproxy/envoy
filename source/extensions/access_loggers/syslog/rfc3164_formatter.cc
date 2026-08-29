#include "source/extensions/access_loggers/syslog/rfc3164_formatter.h"

#include <utility>

#include "source/common/common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view Rfc3164TimestampFormat = "%b %e %T";

} // namespace

const DateFormatter& Rfc3164Formatter::timestampFormatter() {
  static const DateFormatter formatter{Rfc3164TimestampFormat, true};
  return formatter;
}

Rfc3164Formatter::Rfc3164Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                                   std::string priority, std::string hostname, std::string tag)
    : body_formatter_(std::move(body_formatter)), priority_(std::move(priority)),
      hostname_(std::move(hostname)), tag_(std::move(tag)) {}

std::string Rfc3164Formatter::format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  std::string output;
  formatTo(output, context, stream_info);
  return output;
}

void Rfc3164Formatter::formatTo(std::string& output, const Envoy::Formatter::Context& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  absl::StrAppend(&output, priority_,
                  timestampFormatter().fromTime(stream_info.timeSource().systemTime()));
  if (!hostname_.empty()) {
    absl::StrAppend(&output, " ", hostname_);
  }
  absl::StrAppend(&output, " ", tag_, ": ");
  body_formatter_->formatTo(output, context, stream_info);
}

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
