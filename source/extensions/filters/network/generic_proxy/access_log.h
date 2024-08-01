#pragma once

#include "source/extensions/filters/network/generic_proxy/file_access_log.h"
#include "source/extensions/filters/network/generic_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

struct FormatterContext {
  const RequestHeaderFrame* request_{};
  const ResponseHeaderFrame* response_{};

  static constexpr absl::string_view category() { return "generic_proxy"; }
};

// Formatter for generic proxy.
using Formatter = Formatter::FormatterBase<FormatterContext>;
using FormatterProvider = Envoy::Formatter::FormatterProviderBase<FormatterContext>;
using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;
using CommandParser = Envoy::Formatter::CommandParserBase<FormatterContext>;
using CommandParserPtr = Envoy::Formatter::CommandParserBasePtr<FormatterContext>;
using CommandParserFactory = Envoy::Formatter::CommandParserFactoryBase<FormatterContext>;
using BuiltInCommandParserFactory =
    Envoy::Formatter::BuiltInCommandParserFactoryBase<FormatterContext>;

// Access log for generic proxy.
using AccessLogFilter = AccessLog::FilterBase<FormatterContext>;
using AccessLogFilterPtr = std::unique_ptr<AccessLogFilter>;
using AccessLogFilterFactory = AccessLog::ExtensionFilterFactoryBase<FormatterContext>;
using AccessLogInstance = AccessLog::InstanceBase<FormatterContext>;
using AccessLogInstanceSharedPtr = std::shared_ptr<AccessLogInstance>;
using AccessLogInstanceFactory = AccessLog::AccessLogInstanceFactoryBase<FormatterContext>;

// File access log for generic proxy.
using FileAccessLog = FileAccessLogBase<FormatterContext>;
using FileAccessLogFactory = FileAccessLogFactoryBase<FormatterContext>;

class StringValueFormatterProvider : public FormatterProvider {
public:
  using ValueExtractor = std::function<absl::optional<std::string>(const FormatterContext&,
                                                                   const StreamInfo::StreamInfo&)>;

  StringValueFormatterProvider(ValueExtractor f, absl::optional<size_t> max_length = absl::nullopt)
      : value_extractor_(f), max_length_(max_length) {}

  // FormatterProvider
  absl::optional<std::string>
  formatWithContext(const FormatterContext& context,
                    const StreamInfo::StreamInfo& stream_info) const override;
  ProtobufWkt::Value
  formatValueWithContext(const FormatterContext& context,
                         const StreamInfo::StreamInfo& stream_info) const override;

private:
  ValueExtractor value_extractor_;
  absl::optional<size_t> max_length_;
};

class GenericStatusCodeFormatterProvider : public FormatterProvider {
public:
  GenericStatusCodeFormatterProvider() = default;

  // FormatterProvider
  absl::optional<std::string> formatWithContext(const FormatterContext& context,
                                                const StreamInfo::StreamInfo&) const override;
  ProtobufWkt::Value formatValueWithContext(const FormatterContext& context,
                                            const StreamInfo::StreamInfo&) const override;
};

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
