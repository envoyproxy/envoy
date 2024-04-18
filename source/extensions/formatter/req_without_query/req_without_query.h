#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

class ReqWithoutQuery : public ::Envoy::Formatter::FormatterProvider {
public:
  ReqWithoutQuery(const std::string& main_header, const std::string& alternative_header,
                  absl::optional<size_t> max_length);

  absl::optional<std::string>
  formatWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                    const StreamInfo::StreamInfo&) const override;
  ProtobufWkt::Value formatValueWithContext(const Envoy::Formatter::HttpFormatterContext& context,
                                            const StreamInfo::StreamInfo&) const override;

private:
  const Http::HeaderEntry* findHeader(const Http::HeaderMap& headers) const;

  Http::LowerCaseString main_header_;
  Http::LowerCaseString alternative_header_;
  absl::optional<size_t> max_length_;
};

class ReqWithoutQueryCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  ReqWithoutQueryCommandParser() = default;
  ::Envoy::Formatter::FormatterProviderPtr parse(const std::string& command,
                                                 const std::string& subcommand,
                                                 absl::optional<size_t>& max_length) const override;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
