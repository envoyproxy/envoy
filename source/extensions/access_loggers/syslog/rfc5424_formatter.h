#pragma once

#include <string>

#include "envoy/common/time.h"
#include "envoy/formatter/substitution_formatter.h"

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

inline constexpr absl::string_view DefaultRfc5424MessageId = "envoy.access";

/** Formats a complete RFC 5424 syslog message. */
class Rfc5424Formatter : public Formatter::Formatter {
public:
  Rfc5424Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter, std::string priority,
                   std::string hostname, std::string app_name,
                   std::string msg_id = std::string(DefaultRfc5424MessageId));

  std::string format(const Envoy::Formatter::Context& context,
                     const StreamInfo::StreamInfo& stream_info) const override;
  void formatTo(std::string& output, const Envoy::Formatter::Context& context,
                const StreamInfo::StreamInfo& stream_info) const override;

private:
  static const DateFormatter& timestampFormatter();
  static std::string nilValueIfEmpty(std::string value);
  static std::string processId();

  const Envoy::Formatter::FormatterConstSharedPtr body_formatter_;
  const std::string priority_;
  const std::string hostname_;
  const std::string app_name_;
  const std::string proc_id_;
  const std::string msg_id_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
