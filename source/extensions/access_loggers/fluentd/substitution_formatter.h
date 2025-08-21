#pragma once

#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace Fluentd {

/**
 * A formatter for Fluentd logs
 */
class FluentdFormatter {
public:
  virtual ~FluentdFormatter() = default;

  /**
   * @return a vector of bytes representing the Fluentd MessagePack record
   */
  virtual std::vector<uint8_t> format(const Formatter::HttpFormatterContext& context,
                                      const StreamInfo::StreamInfo& stream_info) const PURE;
};

using FluentdFormatterPtr = std::unique_ptr<FluentdFormatter>;

/**
 * A formatter for Fluentd logs. It is expecting to receive a JSON formatter, and converts the
 * JSON formatter message to MessagePack format.
 * TODO(ohadvano): Improve the formatting operation by creating a dedicated formatter that
 *                 will directly serialize the record to msgpack payload.
 */
class FluentdFormatterImpl : public FluentdFormatter {
public:
  FluentdFormatterImpl(Formatter::FormatterPtr json_formatter);

  std::vector<uint8_t> format(const Formatter::HttpFormatterContext& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  Formatter::FormatterPtr json_formatter_;
};

} // namespace Fluentd
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
