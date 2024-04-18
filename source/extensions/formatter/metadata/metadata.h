#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/stream_info_formatter.h"
#include "source/common/formatter/substitution_formatter.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

// Access log handler for METADATA() command.
class MetadataFormatterCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  MetadataFormatterCommandParser();
  ::Envoy::Formatter::FormatterProviderPtr parse(const std::string& command,
                                                 const std::string& subcommand,
                                                 absl::optional<size_t>& max_length) const override;

private:
  // Map used to dispatch types of metadata to individual handlers which will
  // access required metadata object.
  using FormatterProviderFunc = std::function<::Envoy::Formatter::StreamInfoFormatterProviderPtr(
      const std::string& filter_namespace, const std::vector<std::string>& path,
      absl::optional<size_t> max_length)>;
  std::map<std::string, FormatterProviderFunc> metadata_formatter_providers_;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
