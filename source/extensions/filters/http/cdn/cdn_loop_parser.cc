#include "extensions/filters/http/cdn/cdn_loop_parser.h"

#include "absl/status/status.h"
#include "absl/strings/string_view.h"
#include "absl/strings/str_format.h"
#include "common/common/statusor.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cdn {
namespace CdnLoopParser {

namespace {

// RFC 5234 Appendix B.1 says:
//
// ALPHA          =  %x41-5A / %x61-7A   ; A-Z / a-z
constexpr bool IsAlpha(char c) {
  return ('\x41' <= c && c <= '\x5a') || ('\x61' <= c && c <= '\x7a');
}

// RFC 5234 Appendix B.1 says:
//
// DIGIT          =  %x30-39 ; 0-9
constexpr bool IsDigit(char c) { return '\x30' <= c && c <= '\x39'; }

// RFC 2234 Section 6.1 defines HEXDIG as:
//
// HEXDIG         =  DIGIT / "A" / "B" / "C" / "D" / "E" / "F"
//
// This rule allows lower case letters too in violation of the RFC.
constexpr bool IsHexDigitCaseInsensitive(char c) {
  return IsDigit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

// RFC 7230 Section 3.2.6 defines obs-text as:
//
// obs-text       = %x80-FF
constexpr bool IsObsText(char c) { return 0x80 & c; }

// RFC 7230 Section 3.2.6 defines qdtext as:
//
// qdtext         = HTAB / SP /%x21 / %x23-5B / %x5D-7E / obs-text
constexpr bool IsQdText(char c) {
  return c == '\t' || c == ' ' || c == '\x21' || ('\x23' <= c && c <= '\x5B') ||
         ('\x5D' <= c && c <= '\x7E') || IsObsText(c);
}

// RFC 5324 Appendix B.1 says:
//
// VCHAR          =  %x21-7E
//                        ; visible (printing) characters
constexpr bool IsVChar(char c) { return '\x21' <= c && c <= '\x7e'; }

} // namespace

ParseContext ParseOptionalWhitespace(const ParseContext& input) {
  ParseContext context = input;
  while (!context.atEnd()) {
    char c = context.peek();
    if (!(c == ' ' || c == '\t')) {
      break;
    }
    context.increment();
  }
  return context;
}

StatusOr<ParseContext> ParseQuotedPair(const ParseContext& input) {
  ParseContext context = input;
  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected backslash at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '\\') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected backslash at position %d; found '%c'.", input.next(), context.peek()));
  }
  context.increment();

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected escaped character at position %d; found end-of-input", context.next()));
  }

  const char c = context.peek();
  if (!(c == '\t' || c == ' ' || IsVChar(c) || IsObsText(c))) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected escapable character at position %d; found '\\x%x'.", input.next(),
                        context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParseContext> ParseQuotedString(const ParseContext& input) {
  ParseContext context = input;

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected opening '\"' at position %d; found end-of-context", context.next()));
  }

  if (context.peek() != '"') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected opening quote at position %d; found '%c'.", context.next(), context.peek()));
  }
  context.increment();

  while (!context.atEnd() && context.peek() != '"') {
    if (IsQdText(context.peek())) {
      context.increment();
      continue;
    }

    if (StatusOr<ParseContext> quoted_pair_context = ParseQuotedPair(context);
        !quoted_pair_context) {
      return quoted_pair_context.status();
    } else {
      context.update(*quoted_pair_context);
      continue;
    }

    // sub-expressions have parsed as much as they can
    break;
  }

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected closing quote at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '"') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected closing quote at position %d; found '%c'.", input.next(), context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParseContext> ParseToken(const ParseContext& input) {
  ParseContext context = input;
  while (!context.atEnd()) {
    char c = context.peek();
    if (c == '!' || c == '#' || c == '$' || c == '%' || c == '&' || c == '\'' || c == '*' ||
        c == '+' || c == '-' || c == '.' || c == '^' || c == '_' || c == '`' || c == '|' ||
        c == '~' || IsDigit(c) || IsAlpha(c)) {
      context.increment();
    } else {
      break;
    }
  }
  if (context.next() == input.next()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected token starting at position %d.", input.next()));
  }

  return context;
}

StatusOr<ParseContext> ParsePlausibleIpV6(const ParseContext& input) {
  ParseContext context = input;
  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected IPv6 literal at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '[') {
    return absl::InvalidArgumentError(absl::StrFormat("expected opening '[' of IPv6 literal at "
                                                      "position %d; found %c",
                                                      context.next(), context.peek()));
  }
  context.increment();

  while (true) {
    if (context.atEnd()) {
      break;
    }
    char c = context.peek();
    if (!(IsHexDigitCaseInsensitive(c) || c == ':' || c == '.')) {
      break;
    }
    context.increment();
  }

  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected closing ']' of IPv6 literal at position %d "
                        "found end-of-input",
                        context.next()));
  }
  if (context.peek() != ']') {
    return absl::InvalidArgumentError(absl::StrFormat("expected closing ']' of IPv6 literal at "
                                                      "position %d; found %c",
                                                      context.next(), context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParsedCdnId> ParseCdnId(const ParseContext& input) {
  ParseContext context = input;

  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected cdn-id at position %d; found end-of-input", context.next()));
  }

  if (StatusOr<ParseContext> ipv6 = ParsePlausibleIpV6(context); ipv6) {
    context.update(*ipv6);
  } else {
    if (StatusOr<ParseContext> token = ParseToken(context); !token) {
      return token.status();
    } else {
      context.update(*token);
    }
  }

  if (context.atEnd()) {
    return ParsedCdnId(context,
                       context.value().substr(input.next(), context.next() - input.next()));
  }

  if (context.peek() != ':') {
    return ParsedCdnId(context,
                       context.value().substr(input.next(), context.next() - input.next()));
  }
  context.increment();

  while (!context.atEnd()) {
    if (IsDigit(context.value()[context.next()])) {
      context.increment();
    } else {
      break;
    }
  }

  return ParsedCdnId(context, context.value().substr(input.next(), context.next() - input.next()));
}

StatusOr<ParseContext> ParseParameter(const ParseContext& input) {
  ParseContext context = input;

  if (StatusOr<ParseContext> parsed_token = ParseToken(context); !parsed_token) {
    return parsed_token.status();
  } else {
    context.update(*parsed_token);
  }

  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected '=' at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '=') {
    return absl::InvalidArgumentError(absl::StrFormat("expected '=' at position %d; found '%c'.",
                                                      context.next(), context.peek()));
  }
  context.increment();

  if (StatusOr<ParseContext> value_token = ParseToken(context); value_token) {
    return *value_token;
  }

  if (StatusOr<ParseContext> value_quote = ParseQuotedString(context); value_quote) {
    return *value_quote;
  }

  return absl::InvalidArgumentError(
      absl::StrCat("expected token or quoted-string at position %d.", context.next()));
}

StatusOr<ParsedCdnInfo> ParseCdnInfo(const ParseContext& input) {
  absl::string_view cdn_id;
  ParseContext context = input;
  if (StatusOr<ParsedCdnId> parsed_id = ParseCdnId(input); !parsed_id) {
    return parsed_id.status();
  } else {
    context.update(parsed_id->context());
    cdn_id = parsed_id->cdn_id();
  }

  context.update(ParseOptionalWhitespace(context));

  while (!context.atEnd()) {
    if (context.peek() != ';') {
      break;
    }
    context.increment();

    context.update(ParseOptionalWhitespace(context));

    if (StatusOr<ParseContext> parameter = ParseParameter(context); !parameter) {
      return parameter.status();
    } else {
      context.update(*parameter);
    }

    context.update(ParseOptionalWhitespace(context));
  }

  return ParsedCdnInfo(context, cdn_id);
}

StatusOr<ParsedCdnInfoList> ParseCdnInfoList(const ParseContext& input) {
  std::vector<std::string_view> cdn_infos;
  ParseContext context = input;

  context.update(ParseOptionalWhitespace(context));

  while (!context.atEnd()) {
    // Loop invariant: we're always at the beginning of a new element.

    if (context.peek() == ',') {
      // Empty element case
      context.increment();
      context.update(ParseOptionalWhitespace(context));
      continue;
    }

    if (StatusOr<ParsedCdnInfo> parsed_cdn_info = ParseCdnInfo(context); !parsed_cdn_info) {
      return parsed_cdn_info.status();
    } else {
      cdn_infos.push_back(parsed_cdn_info->cdn_id());
      context.update(parsed_cdn_info->context());
    }

    context.update(ParseOptionalWhitespace(context));

    if (context.atEnd()) {
      break;
    }

    if (context.peek() != ',') {
      return absl::InvalidArgumentError(
          absl::StrFormat("expected ',' at position %d", context.next()));
    } else {
      context.increment();
    }

    context.update(ParseOptionalWhitespace(context));
  }

  return ParsedCdnInfoList(context, std::move(cdn_infos));
}

} // namespace CdnLoopParser
} // namespace Cdn
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
