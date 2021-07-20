#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {
#if 0
class MetadataFormatter : public ::Envoy::Formatter::FormatterProvider {
public:
  MetadataFormatter(const std::string& main_header, const std::string& alternative_header,
                  absl::optional<size_t> max_length);

  absl::optional<std::string> format(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                     const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                     absl::string_view) const override;
  ProtobufWkt::Value formatValue(const Http::RequestHeaderMap&, const Http::ResponseHeaderMap&,
                                 const Http::ResponseTrailerMap&, const StreamInfo::StreamInfo&,
                                 absl::string_view) const override;

#if 0
private:
  const Http::HeaderEntry* findHeader(const Http::HeaderMap& headers) const;

  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
#endif
private:
};
#endif

class MetadataFormatterCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  MetadataFormatterCommandParser();
  ::Envoy::Formatter::FormatterProviderPtr parse(const std::string& token, size_t,
                                                 size_t) const override;

private:
  static const size_t MetadataFormatterParamStart{sizeof("METADATA(") - 1};
    std::map<std::string, std::function<::Envoy::Formatter::FormatterProviderPtr(const std::string& filter_namespace, const std::vector<std::string>& path, absl::optional<size_t> max_length)>> metadata_formatter_providers_;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
