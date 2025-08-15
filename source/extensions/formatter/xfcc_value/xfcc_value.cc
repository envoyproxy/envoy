#include "source/extensions/formatter/xfcc_value/xfcc_value.h"

namespace Envoy {
namespace Extensions {
namespace Formatter {

namespace {

const absl::flat_hash_set<std::string>& supportedKeys() {
  // The keys are case-insensitive, so we store them in lower case.
  CONSTRUCT_ON_FIRST_USE(absl::flat_hash_set<std::string>, {
                                                               "by",
                                                               "hash",
                                                               "cert",
                                                               "chain",
                                                               "subject",
                                                               "uri",
                                                               "dns",
                                                           });
}

absl::StatusOr<std::string> parseValueFromXfccByKey(const Http::RequestHeaderMap& headers,
                                                    const Http::LowerCaseString& key) {
  absl::string_view value = headers.getForwardedClientCertValue();
  if (value.empty()) {
    return absl::InvalidArgumentError("XFCC header is not present");
  }

  // The XFCC header value is a comma (`,`) separated string. Each substring is an XFCC element,
  // which holds information added by a single proxy. A proxy can append the current client
  // certificate information as an XFCC element, to the end of the request’s XFCC header after a
  // comma.
  //
  // Each XFCC element is a semicolon (`;`) separated string. Each substring is a key-value pair,
  // grouped together by an equals (`=`) sign. The keys are case-insensitive, the values are
  // case-sensitive. If  `,`, `;` or `=` appear in a value, the value should be double-quoted.
  // Double-quotes in the value should be replaced by backslash-double-quote (`\"`).
  // Only need to find the first value with the key, so scan the header left-to-right.
  absl::string_view::size_type pos = 0;
  bool in_quotes = false;
  while (pos < value.size()) {
    // Find the end of the current element (comma not in quotes).
    absl::string_view::size_type element_end = pos;
    for (; element_end < value.size(); ++element_end) {
      const char c = value[element_end];
      if (c == '"') {
        if (element_end == 0 || value[element_end - 1] != '\\') {
          in_quotes = !in_quotes;
        }
      } else if (c == ',' && !in_quotes) {
        break;
      }
    }

    if (in_quotes) {
      // If we are still in quotes, we have an invalid XFCC header.
      return absl::InvalidArgumentError("Invalid XFCC header: unmatched quotes");
    }

    absl::string_view element = value.substr(pos, element_end - pos);

    // Scan key-value pairs in this element (semicolon not in quotes).
    absl::string_view::size_type e_pos = 0;
    ASSERT(!in_quotes);
    while (e_pos < element.size()) {
      absl::string_view::size_type pair_end = e_pos;
      for (; pair_end < element.size(); ++pair_end) {
        char c = element[pair_end];
        if (c == '"') {
          if (pair_end == 0 || element[pair_end - 1] != '\\') {
            in_quotes = !in_quotes;
          }
        } else if (c == ';' && !in_quotes) {
          break;
        }
      }

      if (in_quotes) {
        // If we are still in quotes, we have an invalid XFCC header.
        return absl::InvalidArgumentError("Invalid XFCC header: unmatched quotes");
      }

      absl::string_view pair = element.substr(e_pos, pair_end - e_pos);

      // Find '=' not in quotes. Because key will always be the first part and won't be double
      // quoted or contain `=`, we can safely use `absl::StrSplit`.
      std::pair<absl::string_view, absl::string_view> key_value = absl::StrSplit(pair, '=', 2);
      if (absl::EqualsIgnoreCase(key_value.first, key.get())) {
        absl::string_view raw_value = absl::StripAsciiWhitespace(key_value.second);
        // If value is double quoted, remove quotes.
        if (raw_value.size() >= 2 && raw_value.front() == '"' && raw_value.back() == '"') {
          // Remove the quotes.
          raw_value = raw_value.substr(1, raw_value.size() - 2);
        }

        // Unescape double quotes.
        // Note: this only handle escaped double quotes, not other escape sequences.
        std::string unescaped;
        for (size_t i = 1; i + 1 < raw_value.size(); ++i) {
          if (raw_value[i] == '\\' && i + 1 < raw_value.size() && raw_value[i + 1] == '"') {
            unescaped.push_back('"');
            ++i;
          } else {
            unescaped.push_back(raw_value[i]);
          }
        }
        return unescaped;
      }

      // Move to next pair.
      e_pos = pair_end + 1;
    }

    // Move to next element.
    pos = element_end + 1;
  }

  return absl::InvalidArgumentError("XFCC header does not contain key");
}

} // namespace

class XfccValueFormatterProvider : public ::Envoy::Formatter::FormatterProvider,
                                   Logger::Loggable<Logger::Id::http> {
public:
  XfccValueFormatterProvider(Http::LowerCaseString&& key) : key_(key) {}

  absl::optional<std::string> formatWithContext(const Envoy::Formatter::Context& context,
                                                const StreamInfo::StreamInfo&) const override {
    auto status_or = parseValueFromXfccByKey(context.requestHeaders(), key_);
    if (!status_or.ok()) {
      ENVOY_LOG(debug, "XFCC value extraction failure: {}", status_or.status().message());
      return absl::nullopt;
    }
    return std::move(status_or.value());
  }

  Protobuf::Value formatValueWithContext(const Envoy::Formatter::Context& context,
                                         const StreamInfo::StreamInfo& stream_info) const override {
    absl::optional<std::string> value = formatWithContext(context, stream_info);
    if (!value.has_value()) {
      return ValueUtil::nullValue();
    }
    Protobuf::Value result;
    result.set_string_value(std::move(value.value()));
    return result;
  }

private:
  Http::LowerCaseString key_;
};

Envoy::Formatter::FormatterProviderPtr
XfccValueFormatterCommandParser::parse(absl::string_view command, absl::string_view subcommand,
                                       absl::optional<size_t>) const {
  // Implementation for parsing the XFCC_VALUE() command.
  if (command != "XFCC_VALUE") {
    return nullptr;
  }

  Http::LowerCaseString lower_subcommand(subcommand);
  if (subcommand.empty()) {
    throw EnvoyException("XFCC_VALUE command requires a subcommand");
  }
  if (!supportedKeys().contains(lower_subcommand.get())) {
    throw EnvoyException(
        absl::StrCat("XFCC_VALUE command does not support subcommand: ", lower_subcommand.get()));
  }
  return std::make_unique<XfccValueFormatterProvider>(std::move(lower_subcommand));
}

class XfccValueFormatterCommandParserFactory
    : public Envoy::Formatter::BuiltInCommandParserFactory {
public:
  XfccValueFormatterCommandParserFactory() = default;
  Envoy::Formatter::CommandParserPtr createCommandParser() const override {
    return std::make_unique<XfccValueFormatterCommandParser>();
  }
  std::string name() const override { return "envoy.built_in_formatters.xfcc_value"; }
};

REGISTER_FACTORY(XfccValueFormatterCommandParserFactory,
                 Envoy::Formatter::BuiltInCommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
