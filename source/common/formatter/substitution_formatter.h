#pragma once

#include <bitset>
#include <functional>
#include <list>
#include <regex>
#include <string>
#include <vector>

#include "envoy/common/time.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/formatter/substitution_formatter.h"
#include "envoy/stream_info/stream_info.h"

#include "source/common/common/utility.h"
#include "source/common/formatter/http_formatter_context.h"
#include "source/common/json/json_loader.h"
#include "source/common/json/json_streamer.h"
#include "source/common/json/json_utility.h"

#include "absl/types/optional.h"
#include "re2/re2.h"

namespace Envoy {
namespace Formatter {

/**
 * FormatterProvider for string literals. It ignores headers and stream info and returns string by
 * which it was initialized.
 */
class PlainStringFormatter : public FormatterProvider {
public:
  PlainStringFormatter(absl::string_view str) { str_.set_string_value(str); }

  // FormatterProvider
  absl::optional<std::string> format(const Context&, const StreamInfo::StreamInfo&) const override {
    return str_.string_value();
  }
  Protobuf::Value formatValue(const Context&, const StreamInfo::StreamInfo&) const override {
    return str_;
  }

private:
  Protobuf::Value str_;
};

/**
 * FormatterProvider for numbers.
 */
class PlainNumberFormatter : public FormatterProvider {
public:
  PlainNumberFormatter(double num) { num_.set_number_value(num); }

  // FormatterProvider
  absl::optional<std::string> format(const Context&, const StreamInfo::StreamInfo&) const override {
    std::string str = absl::StrFormat("%g", num_.number_value());
    return str;
  }
  Protobuf::Value formatValue(const Context&, const StreamInfo::StreamInfo&) const override {
    return num_;
  }

private:
  Protobuf::Value num_;
};

/**
 * Access log format parser.
 */
class SubstitutionFormatParser {
public:
  static absl::StatusOr<std::vector<FormatterProviderPtr>>
  parse(absl::string_view format, const std::vector<CommandParserPtr>& command_parsers = {});
};

inline constexpr absl::string_view DefaultUnspecifiedValueStringView = "-";

/**
 * Composite formatter implementation.
 */
class FormatterImpl : public Formatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;

  static absl::StatusOr<std::unique_ptr<FormatterImpl>>
  create(absl::string_view format, bool omit_empty_values = false,
         const CommandParsers& command_parsers = {});

  // Formatter
  std::string format(const Context& context,
                     const StreamInfo::StreamInfo& stream_info) const override;

protected:
  FormatterImpl(absl::Status& creation_status, absl::string_view format,
                bool omit_empty_values = false, const CommandParsers& command_parsers = {})
      : omit_empty_values_(omit_empty_values) {
    auto providers_or_error = SubstitutionFormatParser::parse(format, command_parsers);
    SET_AND_RETURN_IF_NOT_OK(providers_or_error.status(), creation_status);
    providers_ = std::move(*providers_or_error);
  }

private:
  const bool omit_empty_values_;
  std::vector<FormatterProviderPtr> providers_;
};

class JsonFormatterImpl : public Formatter {
public:
  using CommandParsers = std::vector<CommandParserPtr>;
  using Formatter = FormatterProviderPtr;
  using Formatters = std::vector<Formatter>;

  JsonFormatterImpl(const Protobuf::Struct& struct_format, bool omit_empty_values,
                    const CommandParsers& commands = {});

  // Formatter
  std::string format(const Context& context, const StreamInfo::StreamInfo& info) const override;

private:
  const bool omit_empty_values_;
  using ParsedFormatElement = absl::variant<std::string, Formatters>;
  std::vector<ParsedFormatElement> parsed_elements_;
};

} // namespace Formatter
} // namespace Envoy
