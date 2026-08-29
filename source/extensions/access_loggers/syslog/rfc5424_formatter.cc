#include "source/extensions/access_loggers/syslog/rfc5424_formatter.h"

#include <utility>

#include "envoy/common/platform.h"

#include "source/common/common/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view Rfc5424Version = "1";
constexpr absl::string_view Rfc5424TimestampFormat = "%Y-%m-%dT%H:%M:%E6SZ";
constexpr absl::string_view Rfc5424NilValue = "-";

} // namespace

const DateFormatter& Rfc5424Formatter::timestampFormatter() {
  static const DateFormatter formatter{Rfc5424TimestampFormat};
  return formatter;
}

std::string Rfc5424Formatter::nilValueIfEmpty(std::string value) {
  return value.empty() ? std::string(Rfc5424NilValue) : std::move(value);
}

std::string Rfc5424Formatter::processId() {
#ifdef WIN32
  return absl::StrCat(::GetCurrentProcessId());
#else
  return absl::StrCat(::getpid());
#endif
}

Rfc5424Formatter::Rfc5424Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                                   std::string priority, std::string hostname, std::string app_name,
                                   std::string msg_id)
    : body_formatter_(std::move(body_formatter)), priority_(std::move(priority)),
      hostname_(nilValueIfEmpty(std::move(hostname))),
      app_name_(nilValueIfEmpty(std::move(app_name))), proc_id_(processId()),
      msg_id_(nilValueIfEmpty(std::move(msg_id))) {}

std::string Rfc5424Formatter::format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  std::string output;
  formatTo(output, context, stream_info);
  return output;
}

void Rfc5424Formatter::formatTo(std::string& output, const Envoy::Formatter::Context& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  absl::StrAppend(&output, priority_, Rfc5424Version, " ",
                  timestampFormatter().fromTime(stream_info.timeSource().systemTime()), " ",
                  hostname_, " ", app_name_, " ", proc_id_, " ", msg_id_, " ", Rfc5424NilValue);

  const size_t header_size = output.size();
  output.push_back(' ');
  body_formatter_->formatTo(output, context, stream_info);
  if (output.size() == header_size + 1) {
    output.resize(header_size);
  }
}

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
