#pragma once

#include <string>

#include "envoy/config/typed_config.h"
#include "envoy/registry/registry.h"

#include "source/common/formatter/substitution_format_utility.h"
#include "source/common/formatter/substitution_formatter.h"

#include "absl/container/flat_hash_set.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

enum class AllHeadersType { Request, Response };

class AllHeadersFormatter : public ::Envoy::Formatter::FormatterProvider {
public:
  AllHeadersFormatter(AllHeadersType type, uint32_t max_value_bytes,
                      absl::flat_hash_set<std::string> exclude_headers);

  absl::optional<std::string> format(const Envoy::Formatter::Context& context,
                                     const StreamInfo::StreamInfo&) const override;
  Protobuf::Value formatValue(const Envoy::Formatter::Context& context,
                              const StreamInfo::StreamInfo&) const override;

private:
  Protobuf::Struct buildHeadersStruct(const Http::HeaderMap& headers) const;
  const AllHeadersType type_;
  const uint32_t max_value_bytes_;
  const absl::flat_hash_set<std::string> exclude_headers_;
};

class AllHeadersCommandParser : public ::Envoy::Formatter::CommandParser {
public:
  AllHeadersCommandParser(uint32_t max_value_bytes,
                          absl::flat_hash_set<std::string> exclude_headers);

  ::Envoy::Formatter::FormatterProviderPtr parse(absl::string_view command,
                                                 absl::string_view subcommand,
                                                 absl::optional<size_t> max_length) const override;

private:
  const uint32_t max_value_bytes_;
  const absl::flat_hash_set<std::string> exclude_headers_;
};

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
