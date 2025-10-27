#pragma once

#include "envoy/access_log/access_log_config.h"
#include "envoy/extensions/access_loggers/file/v3/file.pb.h"
#include "envoy/formatter/substitution_formatter.h"

#include "source/extensions/filters/network/generic_proxy/interface/stream.h"

namespace Envoy {
namespace Extensions {
namespace NetworkFilters {
namespace GenericProxy {

struct FormatterContextExtension : public Formatter::Context::Extension {
  FormatterContextExtension(const RequestHeaderFrame* request, const ResponseHeaderFrame* response)
      : request_(request), response_(response) {}
  FormatterContextExtension() = default;

  const RequestHeaderFrame* request_{};
  const ResponseHeaderFrame* response_{};
};

#define CHECK_DATA_OR_RETURN(context, field, return_value)                                         \
  const auto checked_data = context.typedExtension<FormatterContextExtension>();                   \
  if (!checked_data.has_value() || checked_data->field == nullptr) {                               \
    return return_value;                                                                           \
  }

using FormatterProvider = Formatter::FormatterProvider;
using FormatterProviderPtr = std::unique_ptr<FormatterProvider>;
using FormatterContext = Formatter::Context;

class StringValueFormatterProvider : public FormatterProvider {
public:
  using ValueExtractor = std::function<absl::optional<std::string>(const FormatterContext&,
                                                                   const StreamInfo::StreamInfo&)>;

  StringValueFormatterProvider(ValueExtractor f, absl::optional<size_t> max_length = absl::nullopt)
      : value_extractor_(f), max_length_(max_length) {}

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext& context,
                                     const StreamInfo::StreamInfo& stream_info) const override;
  Protobuf::Value formatValue(const FormatterContext& context,
                              const StreamInfo::StreamInfo& stream_info) const override;

private:
  ValueExtractor value_extractor_;
  absl::optional<size_t> max_length_;
};

class GenericStatusCodeFormatterProvider : public FormatterProvider {
public:
  GenericStatusCodeFormatterProvider() = default;

  // FormatterProvider
  absl::optional<std::string> format(const FormatterContext& context,
                                     const StreamInfo::StreamInfo&) const override;
  Protobuf::Value formatValue(const FormatterContext& context,
                              const StreamInfo::StreamInfo&) const override;
};

Formatter::CommandParserPtr createGenericProxyCommandParser();

} // namespace GenericProxy
} // namespace NetworkFilters
} // namespace Extensions
} // namespace Envoy
