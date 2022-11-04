#include "source/common/router/header_parser.h"

#include <cctype>
#include <memory>
#include <regex>
#include <string>

#include "envoy/config/core/v3/base.pb.h"

#include "source/common/common/assert.h"
#include "source/common/formatter/substitution_formatter.h"
#include "source/common/http/header_utility.h"
#include "source/common/http/headers.h"
#include "source/common/json/json_loader.h"
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

HttpHeaderFormatterPtr
parseHttpHeaderFormatter(const envoy::config::core::v3::HeaderValue& header_value) {
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

  // UPSTREAM_METADATA and DYNAMIC_METADATA must be translated from JSON ["a", "b"] format to colon
  // format (a:b)
  std::string final_header_value = HeaderParser::translateMetadataFormat(header_value.value());
  // Change PER_REQUEST_STATE to FILTER_STATE.
  final_header_value = HeaderParser::translatePerRequestState(final_header_value);

  // Let the substitution formatter parse the final_header_value.
  return std::make_unique<HttpHeaderFormatterImpl>(
      std::make_unique<Envoy::Formatter::FormatterImpl>(final_header_value, true));
}

// Implements a state machine to parse custom headers. Each character of the custom header format
// is either literal text (with % escaped as %%) or part of a %VAR% or %VAR(["args"])% expression.
// The statement machine does minimal validation of the arguments (if any) and does not know the
// names of valid variables. Interpretation of the variable name and arguments is delegated to
// StreamInfoHeaderFormatter.
// TODO(cpakulski): parseInternal function is executed only when
// envoy_reloadable_features_unified_header_formatter runtime guard is false. When the guard is
// deprecated, parseInternal function is not needed anymore.
HeaderFormatterPtr parseInternal(const envoy::config::core::v3::HeaderValue& header_value) {
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
    return std::make_unique<PlainHeaderFormatter>("");
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
        formatters.emplace_back(new PlainHeaderFormatter(unescape(literal)));
      }
      start = pos + 1;
      break;

    case ParserState::VariableName:
      // Consume "VAR" from "%VAR%" or "%VAR(...)%"
      if (ch == '%') {
        // Found complete variable name, add formatter.
        formatters.emplace_back(new StreamInfoHeaderFormatter(format.substr(start, pos - start)));
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
        formatters.emplace_back(new StreamInfoHeaderFormatter(format.substr(start, pos - start)));
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
    formatters.emplace_back(new PlainHeaderFormatter(unescape(literal)));
  }

  ASSERT(!formatters.empty());

  if (formatters.size() == 1) {
    return std::move(formatters[0]);
  }

  return std::make_unique<CompoundHeaderFormatter>(std::move(formatters));
}

} // namespace

HeaderParserPtr
HeaderParser::configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value_option : headers_to_add) {
    HeaderAppendAction append_action;

    if (header_value_option.has_append()) {
      // 'append' is set and ensure the 'append_action' value is equal to the default value.
      if (header_value_option.append_action() != HeaderValueOption::APPEND_IF_EXISTS_OR_ADD) {
        throw EnvoyException("Both append and append_action are set and it's not allowed");
      }

      append_action = header_value_option.append().value()
                          ? HeaderValueOption::APPEND_IF_EXISTS_OR_ADD
                          : HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD;
    } else {
      append_action = header_value_option.append_action();
    }

    HttpHeaderFormatterPtr header_formatter;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_header_formatter")) {
      header_formatter = parseHttpHeaderFormatter(header_value_option.header());
    } else {
      // Use "old" implementation of header formatters.
      header_formatter =
          std::make_unique<HttpHeaderFormatterBridge>(parseInternal(header_value_option.header()));
    }
    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value_option.header().key()),
        HeadersToAddEntry{std::move(header_formatter), header_value_option.header().value(),
                          append_action, header_value_option.keep_empty_value()});
  }

  return header_parser;
}

HeaderParserPtr HeaderParser::configure(
    const Protobuf::RepeatedPtrField<envoy::config::core::v3::HeaderValue>& headers_to_add,
    HeaderAppendAction append_action) {
  HeaderParserPtr header_parser(new HeaderParser());

  for (const auto& header_value : headers_to_add) {
    HttpHeaderFormatterPtr header_formatter;
    if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.unified_header_formatter")) {
      header_formatter = parseHttpHeaderFormatter(header_value);
    } else {
      // Use "old" implementation of header formatters.
      header_formatter = std::make_unique<HttpHeaderFormatterBridge>(parseInternal(header_value));
    }
    header_parser->headers_to_add_.emplace_back(
        Http::LowerCaseString(header_value.key()),
        HeadersToAddEntry{std::move(header_formatter), header_value.value(), append_action});
  }

  return header_parser;
}

HeaderParserPtr
HeaderParser::configure(const Protobuf::RepeatedPtrField<HeaderValueOption>& headers_to_add,
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
                                   const Http::RequestHeaderMap& request_headers,
                                   const Http::ResponseHeaderMap& response_headers,
                                   const StreamInfo::StreamInfo& stream_info) const {
  evaluateHeaders(headers, request_headers, response_headers, &stream_info);
}

void HeaderParser::evaluateHeaders(Http::HeaderMap& headers,
                                   const Http::RequestHeaderMap& request_headers,
                                   const Http::ResponseHeaderMap& response_headers,
                                   const StreamInfo::StreamInfo* stream_info) const {
  // Removing headers in the headers_to_remove_ list first makes
  // remove-before-add the default behavior as expected by users.
  for (const auto& header : headers_to_remove_) {
    headers.remove(header);
  }

  // Temporary storage to hold evaluated values of headers to add and replace. This is required
  // to execute all formatters using the original received headers.
  // Only after all the formatters produced the new values of the headers, the headers are set.
  // absl::InlinedVector is optimized for 4 headers. After that it behaves as normal std::vector.
  // It is assumed that most of the use cases will add or modify fairly small number of headers
  // (<=4). If this assumption changes, the number of inlined capacity should be increased.
  // header_formatter_speed_test.cc provides micro-benchmark for evaluating speed of adding and
  // replacing headers and should be used when modifying the code below to access the performance
  // impact of code changes.
  absl::InlinedVector<std::pair<const Http::LowerCaseString&, const std::string>, 4> headers_to_add,
      headers_to_overwrite;
  // value_buffer is used only when stream_info is a valid pointer and stores header value
  // created by a formatter. It is declared outside of 'for' loop for performance reason to avoid
  // stack allocation and unnecessary std::string's memory adjustments for each iteration. The
  // actual value of the header is accessed via 'value' variable which is initialized differently
  // depending whether stream_info and valid or nullptr. Based on performance tests implemented in
  // header_formatter_speed_test.cc this approach strikes the best balance between performance and
  // readability.
  std::string value_buffer;
  for (const auto& [key, entry] : headers_to_add_) {
    absl::string_view value;
    if (stream_info != nullptr) {
      value_buffer = entry.formatter_->format(request_headers, response_headers, *stream_info);
      value = value_buffer;
    } else {
      value = entry.original_value_;
    }
    if (!value.empty() || entry.add_if_empty_) {
      switch (entry.append_action_) {
        PANIC_ON_PROTO_ENUM_SENTINEL_VALUES;
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        headers_to_add.emplace_back(key, value);
        break;
      case HeaderValueOption::ADD_IF_ABSENT:
        if (auto header_entry = headers.get(key); header_entry.empty()) {
          headers_to_add.emplace_back(key, value);
        }
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        headers_to_overwrite.emplace_back(key, value);
        break;
      }
    }
  }

  // First overwrite all headers which need to be overwritten.
  for (const auto& header : headers_to_overwrite) {
    headers.setReferenceKey(header.first, header.second);
  }

  // Now add headers which should be added.
  for (const auto& header : headers_to_add) {
    headers.addReferenceKey(header.first, header.second);
  }
}

Http::HeaderTransforms HeaderParser::getHeaderTransforms(const StreamInfo::StreamInfo& stream_info,
                                                         bool do_formatting) const {
  Http::HeaderTransforms transforms;

  for (const auto& [key, entry] : headers_to_add_) {
    if (do_formatting) {
      const std::string value =
          entry.formatter_->format(*Http::StaticEmptyHeaders::get().request_headers,
                                   *Http::StaticEmptyHeaders::get().response_headers, stream_info);
      if (!value.empty() || entry.add_if_empty_) {
        switch (entry.append_action_) {
        case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
          transforms.headers_to_append_or_add.push_back({key, value});
          break;
        case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
          transforms.headers_to_overwrite_or_add.push_back({key, value});
          break;
        case HeaderValueOption::ADD_IF_ABSENT:
          transforms.headers_to_add_if_absent.push_back({key, value});
          break;
        default:
          break;
        }
      }
    } else {
      switch (entry.append_action_) {
      case HeaderValueOption::APPEND_IF_EXISTS_OR_ADD:
        transforms.headers_to_append_or_add.push_back({key, entry.original_value_});
        break;
      case HeaderValueOption::OVERWRITE_IF_EXISTS_OR_ADD:
        transforms.headers_to_overwrite_or_add.push_back({key, entry.original_value_});
        break;
      case HeaderValueOption::ADD_IF_ABSENT:
        transforms.headers_to_add_if_absent.push_back({key, entry.original_value_});
        break;
      default:
        break;
      }
    }
  }

  transforms.headers_to_remove = headers_to_remove_;

  return transforms;
}

} // namespace Router
} // namespace Envoy
