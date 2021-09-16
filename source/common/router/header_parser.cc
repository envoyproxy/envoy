#include "source/common/router/header_parser.h"

#include <cctype>
#include <memory>
#include <string>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/protobuf/utility.h"

#include "absl/strings/str_cat.h"
#include "absl/strings/str_replace.h"

namespace Envoy {
namespace Router {

namespace {

enum class ParserState {
  Literal,                   // processing literal data
  VariableName,              // consuming a %VAR% name
  ExpectArray,               // expect starting [ in %VAR([...])%
  ExpectString,              // expect starting " in array of strings
  String,                    // consuming an array element string
  ExpectArrayDelimiterOrEnd, // expect array delimiter (,) or end of array (])
  ExpectArgsEnd,             // expect closing ) in %VAR(...)%
  ExpectVariableEnd          // expect closing % in %VAR(...)%
};

std::string unescape(absl::string_view sv) { return absl::StrReplaceAll(sv, {{"%%", "%"}}); }

// Implements a state machine to parse custom headers. Each character of the custom header format
// is either literal text (with % escaped as %%) or part of a %VAR% or %VAR(["args"])% expression.
// The statement machine does minimal validation of the arguments (if any) and does not know the
// names of valid variables. Interpretation of the variable name and arguments is delegated to
// StreamInfoHeaderFormatter.
HeaderFormatterPtr
parseInternal(const envoy::config::core::v3::HeaderValue& header_value,
              const envoy::config::core::v3::HeaderValueOption::HeaderAppendAction append_action) {
  const std::string& key = header_value.key();
  // PGV constraints provide this guarantee.
  ASSERT(!key.empty());
  // We reject :path/:authority rewriting, there is already a well defined mechanism to
  // perform this in the RouteAction, and doing this via request_headers_to_add
  // will cause us to have to worry about interaction with other aspects of the
  // RouteAction, e.g. prefix rewriting. We also reject other :-prefixed
  // headers, since it seems dangerous and there doesn't appear a use case.
  // Host is disallowed as it created confusing and inconsistent behaviors for
  // HTTP/1 and HTTP/2. It could arguably be allowed on the response path.
  if (!Http::HeaderUtility::isModifiableHeader(key)) {
    throw EnvoyException(":-prefixed or host headers may not be modified");
  }

  absl::string_view format(header_value.value());
  if (format.empty()) {
    return std::make_unique<PlainHeaderFormatter>("", append_action);
  }

  std::vector<HeaderFormatterPtr> formatters;

  size_t pos = 0, start = 0;
  ParserState state = ParserState::Literal;
  do {
    const char ch = format[pos];
    const bool has_next_ch = (pos + 1) < format.size();

    switch (state) {
    case ParserState::Literal:
      // Searching for start of %VARIABLE% expression.
      if (ch != '%') {
        break;
      }

      if (!has_next_ch) {
        throw EnvoyException(
            fmt::format("Invalid header configuration. Un-escaped % at position {}", pos));
      }

      if (format[pos + 1] == '%') {
        // Escaped %, skip next character.
        pos++;
        break;
      }

      // Un-escaped %: start of variable name. Create a formatter for preceding characters, if
      // any.
      state = ParserState::VariableName;
      if (pos > start) {
        absl::string_view literal = format.substr(start, pos - start);
        formatters.emplace_back(new PlainHeaderFormatter(unescape(literal), append_action));
      }
      start = pos + 1;
      break;

    case ParserState::VariableName:
      // Consume "VAR" from "%VAR%" or "%VAR(...)%"
      if (ch == '%') {
        // Found complete variable name, add formatter.
        formatters.emplace_back(
            new StreamInfoHeaderFormatter(format.substr(start, pos - start), append_action));
        start = pos + 1;
        state = ParserState::Literal;
        break;
      }

      if (ch == '(') {
        // Variable with arguments, search for start of arg array.
        state = ParserState::ExpectArray;
      }
      break;

    case ParserState::ExpectArray:
      // Skip over whitespace searching for the start of JSON array args.
      if (ch == '[') {
        // Search for first argument string
        state = ParserState::ExpectString;
      } else if (!isspace(ch)) {
        // Consume it as a string argument.
        state = ParserState::String;
      }
      break;

    case ParserState::ExpectArrayDelimiterOrEnd:
      // Skip over whitespace searching for a comma or close bracket.
      if (ch == ',') {
        state = ParserState::ExpectString;
      } else if (ch == ']') {
        state = ParserState::ExpectArgsEnd;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting ',', ']', or whitespace after '{}', but "
            "found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::ExpectString:
      // Skip over whitespace looking for the starting quote of a JSON string.
      if (ch == '"') {
        state = ParserState::String;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting '\"' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::String:
      // Consume a JSON string (ignoring backslash-escaped chars).
      if (ch == '\\') {
        if (!has_next_ch) {
          throw EnvoyException(fmt::format(
              "Invalid header configuration. Un-terminated backslash in JSON string after '{}'",
              absl::StrCat(format.substr(start, pos - start))));
        }

        // Skip escaped char.
        pos++;
      } else if (ch == ')') {
        state = ParserState::ExpectVariableEnd;
      } else if (ch == '"') {
        state = ParserState::ExpectArrayDelimiterOrEnd;
      }
      break;

    case ParserState::ExpectArgsEnd:
      // Search for the closing paren of a %VAR(...)% expression.
      if (ch == ')') {
        state = ParserState::ExpectVariableEnd;
      } else if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting ')' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    case ParserState::ExpectVariableEnd:
      // Search for closing % of a %VAR(...)% expression
      if (ch == '%') {
        formatters.emplace_back(
            new StreamInfoHeaderFormatter(format.substr(start, pos - start), append_action));
        start = pos + 1;
        state = ParserState::Literal;
        break;
      }

      if (!isspace(ch)) {
        throw EnvoyException(fmt::format(
            "Invalid header configuration. Expecting '%' or whitespace after '{}', but found '{}'",
            absl::StrCat(format.substr(start, pos - start)), ch));
      }
      break;

    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  } while (++pos < format.size());

  if (state != ParserState::Literal) {
    // Parsing terminated mid-variable.
    throw EnvoyException(
        fmt::format("Invalid header configuration. Un-terminated variable expression '{}'",
                    absl::StrCat(format.substr(start, pos - start))));
  }

  if (pos > start) {
    // Trailing constant data.
    absl::string_view literal = format.substr(start, pos - start);
    formatters.emplace_back(new PlainHeaderFormatter(unescape(literal), append_action));
  }

  ASSERT(!formatters.empty());

  if (formatters.size() == 1) {
    return std::move(formatters[0]);
  }

  return std::make_unique<CompoundHeaderFormatter>(std::move(formatters), append_action);
}

} // namespace

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    envoy::config::core::v3::HeaderValueOption::HeaderAppendAction append_action =
        header_value_option.append_action();
    // Preserve the old behavior until `append` field is fully deprecated.
    if (header_value_option.has_append()) {
      if (append_action != envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS) {
        throw EnvoyException(fmt::format(
            "Both the `append` and `append_action` fields are set for the header key: {}",
            header_value_option.header().key()));
      }

      // If append is false then we need to set `append_action` to `OVERWRITE_IF_EXISTS`. If append
      // is true then we don't need to do anything as `append_action` defaults to
      // `APPEND_IF_EXISTS`.
      if (!header_value_option.append().value()) {
        append_action = envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS;
      }
    }
    HeaderFormatterPtr header_formatter =
        parseInternal(header_value_option.header(), append_action);
    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()),
        HeadersToAddEntry{std::move(header_formatter), header_value_option.header().value()});
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers_to_add,
    envoy::config::core::v3::HeaderValueOption::HeaderAppendAction append_action) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value : headers_to_add) {
    HeaderFormatterPtr header_formatter = parseInternal(header_value, append_action);
    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value.key()),
        HeadersToAddEntry{std::move(header_formatter), header_value.value()});
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValueOption>& headers_to_add,
    const Protobuf::RepeatedPtrField<std::string>& headers_to_remove) {
  HeaderParserPtr header_parser = configure(headers_to_add);

  for (const auto& header : headers_to_remove) {
    // We reject :-prefix (e.g. :path) removal here. This is dangerous, since other aspects of
    // request finalization assume their existence and they are needed for well-formedness in most
    // cases.
    if (!Http::HeaderUtility::isRemovableHeader(header)) {
      throw EnvoyException(":-prefixed or host headers may not be removed");
    }
    header_parser->headers_to_remove_.emplace_back(header);
  }

  return header_parser;
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const StreamInfo::StreamInfo& stream_info) const {
  evaluateHeaders(headers, &stream_info);
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const StreamInfo::StreamInfo* stream_info) const {
  // Removing headers in the headers_to_remove_ list first makes
  // remove-before-add the default behavior as expected by users.
  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }

  for (const auto& [key, entry] : headers_to_add_) {
    const std::string value =
        stream_info != nullptr ? entry.formatter_->format(*stream_info) : entry.original_value_;
    // We don't have to proceed further if the value is empty.
    if (value.empty()) {
      continue;
    }

    switch (entry.formatter_->appendAction()) {
    case envoy::config::core::v3::HeaderValueOption::ADD_IF_ABSENT:
      // Check whether the header already exist or not. We only
      // need to add the header if it doesn't already exist.
      if (headers.get(key).empty()) {
        headers.setReferenceKey(key, value);
      }
      break;
    case envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS:
      // Append the new value to the existing values if the header
      // already exist otherwise simply add the key-value pair.
      headers.addReferenceKey(key, value);
      break;
    case envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS:
      // Overwrite the new value by discarding any existing values if the
      // header already exist otherwise simply add the key-value pair.
      headers.setReferenceKey(key, value);
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }
}

Http::HeaderTransforms HeaderParser::getHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                         bool do_formatting) const {
  Http::HeaderTransforms transforms;

  for (const auto& [key, entry] : headers_to_add_) {
    std::string value = entry.original_value_;
    if (do_formatting) {
      value = entry.formatter_->format(stream_info);
    }

    // We don't have to proceed further if the value is empty.
    if (value.empty()) {
      continue;
    }

    switch (entry.formatter_->appendAction()) {
    case envoy::config::core::v3::HeaderValueOption::ADD_IF_ABSENT:
      // Headers to be appended if it doesn't already exist.
      transforms.headers_to_add_if_absent.push_back({key, value});
      break;
    case envoy::config::core::v3::HeaderValueOption::APPEND_IF_EXISTS:
      // Headers on which the new value needs to be appended
      // to the existing values if the header already exists.
      transforms.headers_to_append_if_exist.push_back({key, value});
      break;
    case envoy::config::core::v3::HeaderValueOption::OVERWRITE_IF_EXISTS:
      // Headers on which the new value needs to be overwritten by
      // discarding any existing values if the header already exists.
      transforms.headers_to_overwrite_if_exist.push_back({key, value});
      break;
    default:
      NOT_REACHED_GCOVR_EXCL_LINE;
    }
  }

  transforms.headers_to_remove = headers_to_remove_;

  return transforms;
}

} // namespace Router
} // namespace Envoy
