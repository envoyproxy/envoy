#include "source/extensions/formatter/xfcc_value/xfcc_value.h"

#include <cstddef>
#include <ranges>

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

size_t countBackslashes(absl::string_view str) {
  size_t count = 0;
  // Search from end to start for first not '\'
  for (char r : std::ranges::reverse_view(str)) {
    if (r == '\\') {
      ++count;
    } else {
      break;
    }
  }
  return count;
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
//
// There maybe multiple XFCC elements and the oldest/leftest one with the key will be used by
// default because Envoy assumes the oldest XFCC element come from the original client certificate.
// So scan the header left-to-right.

// Handles a single key/value pair within an XFCC element.
absl::optional<std::string> parseKeyValuePair(absl::string_view pair, absl::string_view target) {
  // Find '=' not in quotes. Because key will always be the first part and won't be double
  // quoted or contain `=`, we can safely use `absl::StrSplit`.
  std::pair<absl::string_view, absl::string_view> key_value =
      absl::StrSplit(pair, absl::MaxSplits('=', 1));
  absl::string_view raw_key = absl::StripAsciiWhitespace(key_value.first);
  if (!absl::EqualsIgnoreCase(raw_key, target)) {
    return absl::nullopt;
  }

  absl::string_view raw_value = absl::StripAsciiWhitespace(key_value.second);
  // If value is double quoted, remove quotes.
  if (raw_value.size() >= 2 && raw_value.front() == '"' && raw_value.back() == '"') {
    raw_value = raw_value.substr(1, raw_value.size() - 2);
  }

  // Quick path to avoid handle unescaping if not needed.
  if (raw_value.find('\\') == absl::string_view::npos) {
    return std::string(raw_value);
  }

  // Handle unescaping.

  // If the raw value only contains a single backslash then return it as is.
  if (raw_value.size() < 2) {
    return std::string(raw_value);
  }

  // Unescape double quotes and backslashes.
  std::string unescaped;
  unescaped.reserve(raw_value.size());
  size_t i = 0;
  for (; i < raw_value.size() - 1; ++i) {
    if (raw_value[i] == '\\') {
      if (raw_value[i + 1] == '"' || raw_value[i + 1] == '\\') {
        unescaped.push_back(raw_value[i + 1]);
        ++i;
        continue;
      }
    }
    unescaped.push_back(raw_value[i]);
  }
  // Handle the last character.
  if (i < raw_value.size()) {
    unescaped.push_back(raw_value[i]);
  }

  return unescaped;
}

// Handles a single XFCC element (semicolon-separated key/value pairs).
absl::optional<std::string> parseElementForKey(absl::string_view element,
                                               absl::string_view target) {

  // Scan key-value pairs in this element (by semicolon not in quotes).
  bool in_quotes = false;
  size_t start = 0;
  const size_t element_size = element.size();
  for (size_t i = 0; i <= element_size; ++i) {
    // Check for end of key-value pair.
    if (i == element_size || element[i] == ';') {
      // If not in quotes then we found the end of a key-value pair.
      if (!in_quotes) {
        auto value = parseKeyValuePair(element.substr(start, i - start), target);
        if (value.has_value()) {
          return value;
        }
        start = i + 1;
      }
      continue;
    }

    // Switch quote state if we encounter a quote character.
    if (element[i] == '"') {
      if (countBackslashes(element.substr(0, i)) % 2 == 0) {
        in_quotes = !in_quotes;
      }
    }
  }

  // Note, we should never encounter unmatched quotes here because if there is
  // an unmatched quote, it should be handled in the parseValueFromXfccByKey()
  // and will not enter this function.
  ASSERT(!in_quotes);
  return absl::nullopt;
}

// Extracts the key from the XFCC header.
absl::StatusOr<std::string> parseValueFromXfccByKey(const Http::RequestHeaderMap& headers,
                                                    absl::string_view target) {
  absl::string_view value = headers.getForwardedClientCertValue();
  if (value.empty()) {
    return absl::InvalidArgumentError("XFCC header is not present");
  }

  // Scan elements in the XFCC header (by comma not in quotes).
  bool in_quotes = false;
  size_t start = 0;
  const size_t value_size = value.size();
  for (size_t i = 0; i <= value_size; ++i) {
    // Check for end of element.
    if (i == value_size || value[i] == ',') {
      // If not in quotes then we found the end of an element.
      if (!in_quotes) {
        auto result = parseElementForKey(value.substr(start, i - start), target);
        if (result.has_value()) {
          return result.value();
        }
        start = i + 1;
      }
      continue;
    }

    // Switch quote state if we encounter a quote character.
    if (value[i] == '"') {
      if (countBackslashes(value.substr(0, i)) % 2 == 0) {
        in_quotes = !in_quotes;
      }
    }
  }

  if (in_quotes) {
    return absl::InvalidArgumentError("Invalid XFCC header: unmatched quotes");
  }

  return absl::InvalidArgumentError("XFCC header does not contain target key");
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

class XfccValueCommandParserFactory : public Envoy::Formatter::BuiltInCommandParserFactory {
public:
  XfccValueCommandParserFactory() = default;
  Envoy::Formatter::CommandParserPtr createCommandParser() const override {
    return std::make_unique<XfccValueFormatterCommandParser>();
  }
  std::string name() const override { return "envoy.built_in_formatters.xfcc_value"; }
};

REGISTER_FACTORY(XfccValueCommandParserFactory, Envoy::Formatter::BuiltInCommandParserFactory);

} // namespace Formatter
} // namespace Extensions
} // namespace Envoy
