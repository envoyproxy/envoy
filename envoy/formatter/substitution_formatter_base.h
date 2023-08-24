#pragma once

#include <memory>
#include <string>

#include "envoy/access_log/access_log.h"
#include "envoy/common/pure.h"
#include "envoy/config/typed_config.h"
#include "envoy/server/factory_context.h"
#include "envoy/stream_info/stream_info.h"

namespace Envoy {
namespace Formatter {

/**
 * Template interface for multiple protocols/modules formatters.
 */
template <class FormatterContext> class FormatterBase {
public:
  virtual ~FormatterBase() = default;

  /**
   * Return a formatted substitution line.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return std::string string containing the complete formatted substitution line.
   */
  virtual std::string formatWithContext(const FormatterContext& context,
                                        const StreamInfo::StreamInfo& stream_info) const PURE;
};

template <class FormatterContext>
using FormatterBasePtr = std::unique_ptr<FormatterBase<FormatterContext>>;

/**
 * Template interface for multiple protocols/modules formatter providers.
 */
template <class FormatterContext> class FormatterProviderBase {
public:
  virtual ~FormatterProviderBase() = default;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return absl::optional<std::string> optional string containing a single value extracted from
   *         the given context and stream info.
   */
  virtual absl::optional<std::string>
  formatWithContext(const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const PURE;

  /**
   * Format the value with the given context and stream info.
   * @param context supplies the formatter context.
   * @param stream_info supplies the stream info.
   * @return ProtobufWkt::Value containing a single value extracted from the given
   *         context and stream info.
   */
  virtual ProtobufWkt::Value
  formatValueWithContext(const FormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const PURE;
};

template <class FormatterContext>
using FormatterProviderBasePtr = std::unique_ptr<FormatterProviderBase<FormatterContext>>;

} // namespace Formatter
} // namespace Envoy
