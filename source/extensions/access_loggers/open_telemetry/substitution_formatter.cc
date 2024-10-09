#include "source/extensions/access_loggers/open_telemetry/substitution_formatter.h"

#include <algorithm>
#include <list>
#include <string>
#include <vector>

#include "envoy/stream_info/stream_info.h"

#include "source/common/common/assert.h"
#include "source/common/formatter/substitution_formatter.h"

#include "absl/strings/str_join.h"
#include "opentelemetry/proto/common/v1/common.pb.h"

static const std::string DefaultUnspecifiedValueString = "-";

namespace Envoy {
namespace Extensions {
namespace AccessLoggers {
namespace OpenTelemetry {

OpenTelemetryFormatter::OpenTelemetryFormatter(
    const ::opentelemetry::proto::common::v1::KeyValueList& format_mapping,
    const std::vector<Formatter::CommandParserPtr>& commands)
    : kv_list_output_format_(FormatBuilder(commands).toFormatMapValue(format_mapping)) {}

OpenTelemetryFormatter::OpenTelemetryFormatMapWrapper
OpenTelemetryFormatter::FormatBuilder::toFormatMapValue(
    const ::opentelemetry::proto::common::v1::KeyValueList& kv_list_format) const {
  auto output = std::make_unique<OpenTelemetryFormatMap>();
  for (const auto& pair : kv_list_format.values()) {
    switch (pair.value().value_case()) {
    case ::opentelemetry::proto::common::v1::AnyValue::kStringValue:
      output->emplace_back(pair.key(), toFormatStringValue(pair.value().string_value()));
      break;

    case ::opentelemetry::proto::common::v1::AnyValue::kKvlistValue:
      output->emplace_back(pair.key(), toFormatMapValue(pair.value().kvlist_value()));
      break;

    case ::opentelemetry::proto::common::v1::AnyValue::kArrayValue:
      output->emplace_back(pair.key(), toFormatListValue(pair.value().array_value()));
      break;

    default:
      throw EnvoyException("Only string values, nested key value lists and array values are "
                           "supported in OpenTelemetry access log format.");
    }
  }
  return {std::move(output)};
}

OpenTelemetryFormatter::OpenTelemetryFormatListWrapper
OpenTelemetryFormatter::FormatBuilder::toFormatListValue(
    const ::opentelemetry::proto::common::v1::ArrayValue& list_value_format) const {
  auto output = std::make_unique<OpenTelemetryFormatList>();
  for (const auto& value : list_value_format.values()) {
    switch (value.value_case()) {
    case ::opentelemetry::proto::common::v1::AnyValue::kStringValue:
      output->emplace_back(toFormatStringValue(value.string_value()));
      break;

    case ::opentelemetry::proto::common::v1::AnyValue::kKvlistValue:
      output->emplace_back(toFormatMapValue(value.kvlist_value()));
      break;

    case ::opentelemetry::proto::common::v1::AnyValue::kArrayValue:
      output->emplace_back(toFormatListValue(value.array_value()));
      break;
    default:
      throw EnvoyException("Only string values, nested key value lists and array values are "
                           "supported in OpenTelemetry access log format.");
    }
  }
  return {std::move(output)};
}

std::vector<Formatter::FormatterProviderPtr>
OpenTelemetryFormatter::FormatBuilder::toFormatStringValue(const std::string& string_format) const {
  return THROW_OR_RETURN_VALUE(Formatter::SubstitutionFormatParser::parse(string_format, commands_),
                               std::vector<Formatter::FormatterProviderPtr>);
}

::opentelemetry::proto::common::v1::AnyValue OpenTelemetryFormatter::providersCallback(
    const std::vector<Formatter::FormatterProviderPtr>& providers,
    const Formatter::HttpFormatterContext& context, const StreamInfo::StreamInfo& info) const {
  ASSERT(!providers.empty());
  ::opentelemetry::proto::common::v1::AnyValue output;
  std::vector<std::string> bits(providers.size());

  std::transform(
      providers.begin(), providers.end(), bits.begin(),
      [&](const Formatter::FormatterProviderPtr& provider) {
        return provider->formatWithContext(context, info).value_or(DefaultUnspecifiedValueString);
      });

  output.set_string_value(absl::StrJoin(bits, ""));
  return output;
}

::opentelemetry::proto::common::v1::AnyValue OpenTelemetryFormatter::openTelemetryFormatMapCallback(
    const OpenTelemetryFormatter::OpenTelemetryFormatMapWrapper& format_map,
    const OpenTelemetryFormatter::OpenTelemetryFormatMapVisitor& visitor) const {
  ::opentelemetry::proto::common::v1::AnyValue output;
  auto* kv_list = output.mutable_kvlist_value();
  for (const auto& pair : *format_map.value_) {
    ::opentelemetry::proto::common::v1::AnyValue value = absl::visit(visitor, pair.second);
    auto* kv = kv_list->add_values();
    kv->set_key(pair.first);
    *kv->mutable_value() = value;
  }
  return output;
}

::opentelemetry::proto::common::v1::AnyValue
OpenTelemetryFormatter::openTelemetryFormatListCallback(
    const OpenTelemetryFormatter::OpenTelemetryFormatListWrapper& format_list,
    const OpenTelemetryFormatter::OpenTelemetryFormatMapVisitor& visitor) const {
  ::opentelemetry::proto::common::v1::AnyValue output;
  auto* array_value = output.mutable_array_value();
  for (const auto& val : *format_list.value_) {
    ::opentelemetry::proto::common::v1::AnyValue value = absl::visit(visitor, val);
    *array_value->add_values() = value;
  }
  return output;
}

::opentelemetry::proto::common::v1::KeyValueList
OpenTelemetryFormatter::format(const Formatter::HttpFormatterContext& context,
                               const StreamInfo::StreamInfo& info) const {
  OpenTelemetryFormatMapVisitor visitor{
      [&](const std::vector<Formatter::FormatterProviderPtr>& providers) {
        return providersCallback(providers, context, info);
      },
      [&, this](const OpenTelemetryFormatter::OpenTelemetryFormatMapWrapper& format_map) {
        return openTelemetryFormatMapCallback(format_map, visitor);
      },
      [&, this](const OpenTelemetryFormatter::OpenTelemetryFormatListWrapper& format_list) {
        return openTelemetryFormatListCallback(format_list, visitor);
      },
  };
  return openTelemetryFormatMapCallback(kv_list_output_format_, visitor).kvlist_value();
}

} // namespace OpenTelemetry
} // namespace AccessLoggers
} // namespace Extensions
} // namespace Envoy
