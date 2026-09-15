#include "source/extensions/access_loggers/syslog/formatter.h"

#include <utility>

#include "envoy/common/platform.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/substitution_format_utility.h"

#include "absl/strings/str_format.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

namespace {

constexpr absl::string_view DefaultAppName = "envoy";
constexpr absl::string_view DefaultRfc5424MessageId = "envoy.access";
constexpr absl::string_view Rfc3164TimestampFormat = "%b %e %T";
constexpr absl::string_view Rfc5424NilValue = "-";
constexpr absl::string_view Rfc5424TimestampFormat = "%Y-%m-%dT%H:%M:%E6SZ";
constexpr absl::string_view Rfc5424Version = "1";

uint8_t facilityCode(SyslogAccessLogConfig::Facility facility) {
  if (facility == SyslogAccessLogConfig::FACILITY_LOCAL7) {
    return 23;
  }
  return static_cast<uint8_t>(facility) - 1;
}

uint8_t severityCode(SyslogAccessLogConfig::Severity severity) {
  if (severity == SyslogAccessLogConfig::INFO) {
    return 6;
  }
  if (severity == SyslogAccessLogConfig::DEBUG) {
    return 7;
  }
  return static_cast<uint8_t>(severity) - 1;
}

uint8_t pri(uint8_t facility, uint8_t severity) {
  return static_cast<uint8_t>((facility << 3) | severity);
}

std::string processId() {
#ifdef WIN32
  return absl::StrFormat("%u", ::GetCurrentProcessId());
#else
  return absl::StrFormat("%d", ::getpid());
#endif
}

const DateFormatter& rfc3164TimestampFormatter() {
  static const DateFormatter formatter{Rfc3164TimestampFormat, true};
  return formatter;
}

const DateFormatter& rfc5424TimestampFormatter() {
  static const DateFormatter formatter{Rfc5424TimestampFormat};
  return formatter;
}

} // namespace

Rfc3164HeaderFormatter::Rfc3164HeaderFormatter(SyslogAccessLogConfig::Facility facility,
                                               SyslogAccessLogConfig::Severity severity,
                                               bool omit_hostname, absl::string_view tag)
    : facility_(facilityCode(facility)), severity_(severityCode(severity)),
      priority_(pri(facility_, severity_)),
      hostname_(omit_hostname ? ""
                              : Formatter::SubstitutionFormatUtils::getHostname().value_or("")),
      tag_(tag.empty() ? std::string(DefaultAppName) : std::string(tag)) {}

std::string Rfc3164HeaderFormatter::format(SystemTime timestamp) const {
  std::string output;
  formatTo(output, timestamp);
  return output;
}

void Rfc3164HeaderFormatter::formatTo(std::string& output, SystemTime timestamp) const {
  const std::string formatted_timestamp = rfc3164TimestampFormatter().fromTime(timestamp);
  if (hostname_.empty()) {
    absl::StrAppendFormat(&output, "<%d>%s %s: ", priority_, formatted_timestamp, tag_);
    return;
  }
  absl::StrAppendFormat(&output, "<%d>%s %s %s: ", priority_, formatted_timestamp, hostname_, tag_);
}

Rfc5424HeaderFormatter::Rfc5424HeaderFormatter(SyslogAccessLogConfig::Facility facility,
                                               SyslogAccessLogConfig::Severity severity,
                                               bool omit_hostname, absl::string_view tag,
                                               absl::string_view msg_id)
    : facility_(facilityCode(facility)), severity_(severityCode(severity)),
      priority_(pri(facility_, severity_)),
      hostname_(omit_hostname ? std::string(Rfc5424NilValue)
                              : Formatter::SubstitutionFormatUtils::getHostname().value_or(
                                    std::string(Rfc5424NilValue))),
      app_name_(tag.empty() ? std::string(DefaultAppName) : std::string(tag)),
      proc_id_(processId()),
      msg_id_(msg_id.empty() ? std::string(DefaultRfc5424MessageId) : std::string(msg_id)) {}

std::string Rfc5424HeaderFormatter::format(SystemTime timestamp) const {
  std::string output;
  formatTo(output, timestamp);
  return output;
}

void Rfc5424HeaderFormatter::formatTo(std::string& output, SystemTime timestamp) const {
  absl::StrAppendFormat(&output, "<%d>%s %s %s %s %s %s %s", priority_, Rfc5424Version,
                        rfc5424TimestampFormatter().fromTime(timestamp), hostname_, app_name_,
                        proc_id_, msg_id_, Rfc5424NilValue);
}

Rfc3164Formatter::Rfc3164Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                                   Rfc3164HeaderFormatter header_formatter)
    : body_formatter_(std::move(body_formatter)), header_formatter_(std::move(header_formatter)) {}

std::string Rfc3164Formatter::format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  std::string output;
  formatTo(output, context, stream_info);
  return output;
}

void Rfc3164Formatter::formatTo(std::string& output, const Envoy::Formatter::Context& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  header_formatter_.formatTo(output, stream_info.timeSource().systemTime());
  body_formatter_->formatTo(output, context, stream_info);
}

Rfc5424Formatter::Rfc5424Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                                   Rfc5424HeaderFormatter header_formatter)
    : body_formatter_(std::move(body_formatter)), header_formatter_(std::move(header_formatter)) {}

std::string Rfc5424Formatter::format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo& stream_info) const {
  std::string output;
  formatTo(output, context, stream_info);
  return output;
}

void Rfc5424Formatter::formatTo(std::string& output, const Envoy::Formatter::Context& context,
                                const StreamInfo::StreamInfo& stream_info) const {
  header_formatter_.formatTo(output, stream_info.timeSource().systemTime());

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
