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
  MetadataFormatterCommandParser() = default;
  ::Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                                 absl::string_view subcommand,
                                                 absl::optional<size_t> max_length) const override;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
