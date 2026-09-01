#pragma once

#include <cstdint>
#include <string>

#include "envoy/common/time.h"
#include "envoy/extensions/access_loggers/syslog/v3/syslog.pb.h"
#include "envoy/formatter/substitution_formatter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

using SyslogAccessLogConfig = envoy::extensions::access_loggers::syslog::v3::SyslogAccessLogConfig;

/** Immutable RFC 3164 header fields. */
class Rfc3164HeaderFormatter {
public:
  Rfc3164HeaderFormatter(SyslogAccessLogConfig::Facility facility,
                         SyslogAccessLogConfig::Severity severity, bool omit_hostname,
                         absl::string_view tag);

  std::string format(SystemTime timestamp) const;
  void formatTo(std::string& output, SystemTime timestamp) const;

private:
  uint8_t facility_;
  uint8_t severity_;
  uint8_t priority_;
  std::string hostname_;
  std::string tag_;
};

/** Immutable RFC 5424 header fields. */
class Rfc5424HeaderFormatter {
public:
  Rfc5424HeaderFormatter(SyslogAccessLogConfig::Facility facility,
                         SyslogAccessLogConfig::Severity severity, bool omit_hostname,
                         absl::string_view tag, absl::string_view msg_id);

  std::string format(SystemTime timestamp) const;
  void formatTo(std::string& output, SystemTime timestamp) const;

private:
  uint8_t facility_;
  uint8_t severity_;
  uint8_t priority_;
  std::string hostname_;
  std::string app_name_;
  std::string proc_id_;
  std::string msg_id_;
};

/** Formats a complete RFC 3164-style syslog message. */
class Rfc3164Formatter : public Formatter::Formatter {
public:
  Rfc3164Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                   Rfc3164HeaderFormatter header_formatter);

  std::string format(const Envoy::Formatter::Context& context,
                     const StreamInfo::StreamInfo& stream_info) const override;
  void formatTo(std::string& output, const Envoy::Formatter::Context& context,
                const StreamInfo::StreamInfo& stream_info) const override;

private:
  const Envoy::Formatter::FormatterConstSharedPtr body_formatter_;
  const Rfc3164HeaderFormatter header_formatter_;
};

/** Formats a complete RFC 5424 syslog message. */
class Rfc5424Formatter : public Formatter::Formatter {
public:
  Rfc5424Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter,
                   Rfc5424HeaderFormatter header_formatter);

  std::string format(const Envoy::Formatter::Context& context,
                     const StreamInfo::StreamInfo& stream_info) const override;
  void formatTo(std::string& output, const Envoy::Formatter::Context& context,
                const StreamInfo::StreamInfo& stream_info) const override;

private:
  const Envoy::Formatter::FormatterConstSharedPtr body_formatter_;
  const Rfc5424HeaderFormatter header_formatter_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
