#pragma once

#include <string>

#include "envoy/common/time.h"
#include "envoy/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Syslog {

/** Formats a complete RFC 3164-style syslog message. */
class Rfc3164Formatter : public Formatter::Formatter {
public:
  Rfc3164Formatter(Envoy::Formatter::FormatterConstSharedPtr body_formatter, std::string priority,
                   std::string hostname, std::string tag);

  std::string format(const Envoy::Formatter::Context& context,
                     const StreamInfo::StreamInfo& stream_info) const override;
  void formatTo(std::string& output, const Envoy::Formatter::Context& context,
                const StreamInfo::StreamInfo& stream_info) const override;

private:
  static const DateFormatter& timestampFormatter();

  const Envoy::Formatter::FormatterConstSharedPtr body_formatter_;
  const std::string priority_;
  const std::string hostname_;
  const std::string tag_;
};

} // namespace Syslog
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
