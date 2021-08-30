#include "source/extensions/filters/http/cdn_loop/parser.h"

#include "source/common/common/statusor.h"

#include "absl/status/status.h"
#include "absl/strings/str_format.h"
#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {
namespace Parser {

namespace {

// RFC 5234 Appendix B.1 says:
//
// ALPHA          =  %x41-5A / %x61-7A   ; A-Z / a-z
constexpr bool isAlpha(char c) {
  return ('\x41' <= c && c <= '\x5a') || ('\x61' <= c && c <= '\x7a');
}

// RFC 5234 Appendix B.1 says:
//
// DIGIT          =  %x30-39 ; 0-9
constexpr bool isDigit(char c) { return '\x30' <= c && c <= '\x39'; }

// RFC 2234 Section 6.1 defines HEXDIG as:
//
// HEXDIG         =  DIGIT / "A" / "B" / "C" / "D" / "E" / "F"
//
// This rule allows lower case letters too in violation of the RFC since IPv6
// addresses commonly contain lower-case hex digits.
constexpr bool isHexDigitCaseInsensitive(char c) {
  return isDigit(c) || ('A' <= c && c <= 'F') || ('a' <= c && c <= 'f');
}

// RFC 7230 Section 3.2.6 defines obs-text as:
//
// obs-text       = %x80-FF
constexpr bool isObsText(char c) { return 0x80 & c; }

// RFC 7230 Section 3.2.6 defines qdtext as:
//
// qdtext         = HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text
constexpr bool isQdText(char c) {
  return c == '\t' || c == ' ' || c == '\x21' || ('\x23' <= c && c <= '\x5B') ||
         ('\x5D' <= c && c <= '\x7E') || isObsText(c);
}

// RFC 5234 Appendix B.1 says:
//
// VCHAR          =  %x21-7E
//                        ; visible (printing) characters
constexpr bool isVChar(char c) { return '\x21' <= c && c <= '\x7e'; }

} // namespace

ParseContext skipOptionalWhitespace(const ParseContext& input) {
  ParseContext context = input;
  while (!context.atEnd()) {
    const char c = context.peek();
    if (!(c == ' ' || c == '\t')) {
      break;
    }
    context.increment();
  }
  return context;
}

StatusOr<ParseContext> parseQuotedPair(const ParseContext& input) {
  ParseContext context = input;
  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected backslash at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '\\') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected backslash at position %d; found '%c'", input.next(), context.peek()));
  }
  context.increment();

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected escaped character at position %d; found end-of-input", context.next()));
  }

  const char c = context.peek();
  if (!(c == '\t' || c == ' ' || isVChar(c) || isObsText(c))) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected escapable character at position %d; found '\\x%x'", input.next(),
                        context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParseContext> parseQuotedString(const ParseContext& input) {
  ParseContext context = input;

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected opening '\"' at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '"') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected opening quote at position %d; found '%c'", context.next(), context.peek()));
  }
  context.increment();

  while (!context.atEnd() && context.peek() != '"') {
    if (isQdText(context.peek())) {
      context.increment();
      continue;
    } else if (context.peek() == '\\') {
      if (StatusOr<ParseContext> quoted_pair_context = parseQuotedPair(context);
          !quoted_pair_context.ok()) {
        return quoted_pair_context.status();
      } else {
        context.setNext(*quoted_pair_context);
        continue;
      }
    } else {
      break;
    }
  }

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected closing quote at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '"') {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected closing quote at position %d; found '%c'", input.next(), context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParseContext> parseToken(const ParseContext& input) {
  ParseContext context = input;
  while (!context.atEnd()) {
    const char c = context.peek();
    // Put alphanumeric, -, and _ characters at the head of the list since
    // they're likely to be used most often.
    if (isAlpha(c) || isDigit(c) || c == '-' || c == '_' || c == '!' || c == '#' || c == '$' ||
        c == '%' || c == '&' || c == '\'' || c == '*' || c == '+' || c == '.' || c == '^' ||
        c == '`' || c == '|' || c == '~') {
      context.increment();
    } else {
      break;
    }
  }
  if (context.next() == input.next()) {
    if (context.atEnd()) {
      return absl::InvalidArgumentError(absl::StrFormat(
          "expected token starting at position %d; found end of input", input.next()));
    } else {
      return absl::InvalidArgumentError(absl::StrFormat(
          "expected token starting at position %d; found '%c'", input.next(), context.peek()));
    }
  }

  return context;
}

StatusOr<ParseContext> parsePlausibleIpV6(const ParseContext& input) {
  ParseContext context = input;
  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrFormat(
        "expected IPv6 literal at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '[') {
    return absl::InvalidArgumentError(absl::StrFormat("expected opening '[' of IPv6 literal at "
                                                      "position %d; found '%c'",
                                                      context.next(), context.peek()));
  }
  context.increment();

  while (true) {
    if (context.atEnd()) {
      break;
    }
    const char c = context.peek();
    if (!(isHexDigitCaseInsensitive(c) || c == ':' || c == '.')) {
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
                                                      "position %d; found '%c'",
                                                      context.next(), context.peek()));
  }
  context.increment();

  return context;
}

StatusOr<ParsedCdnId> parseCdnId(const ParseContext& input) {
  ParseContext context = input;

  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected cdn-id at position %d; found end-of-input", context.next()));
  }

  // Optimization: dispatch on the next character to avoid the StrFormat in the
  // error path of an IPv6 parser when the value has a token (and vice versa).
  if (context.peek() == '[') {
    if (StatusOr<ParseContext> ipv6 = parsePlausibleIpV6(context); !ipv6.ok()) {
      return ipv6.status();
    } else {
      context.setNext(*ipv6);
    }
  } else {
    if (StatusOr<ParseContext> token = parseToken(context); !token.ok()) {
      return token.status();
    } else {
      context.setNext(*token);
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
    if (isDigit(context.value()[context.next()])) {
      context.increment();
    } else {
      break;
    }
  }

  return ParsedCdnId(context, context.value().substr(input.next(), context.next() - input.next()));
}

StatusOr<ParseContext> parseParameter(const ParseContext& input) {
  ParseContext context = input;

  if (StatusOr<ParseContext> parsed_token = parseToken(context); !parsed_token.ok()) {
    return parsed_token.status();
  } else {
    context.setNext(*parsed_token);
  }

  if (context.atEnd()) {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected '=' at position %d; found end-of-input", context.next()));
  }

  if (context.peek() != '=') {
    return absl::InvalidArgumentError(
        absl::StrFormat("expected '=' at position %d; found '%c'", context.next(), context.peek()));
  }
  context.increment();

  if (context.atEnd()) {
    return absl::InvalidArgumentError(absl::StrCat(
        "expected token or quoted-string at position %d; found end-of-input", context.next()));
  }

  // Optimization: dispatch on the next character to avoid the StrFormat in the
  // error path of an quoted string parser when the next item is a token (and
  // vice versa).
  if (context.peek() == '"') {
    if (StatusOr<ParseContext> value_quote = parseQuotedString(context); !value_quote.ok()) {
      return value_quote.status();
    } else {
      return *value_quote;
    }
  } else {
    if (StatusOr<ParseContext> value_token = parseToken(context); !value_token.ok()) {
      return value_token.status();
    } else {
      return *value_token;
    }
  }
}

StatusOr<ParsedCdnInfo> parseCdnInfo(const ParseContext& input) {
  absl::string_view cdn_id;
  ParseContext context = input;
  if (StatusOr<ParsedCdnId> parsed_id = parseCdnId(input); !parsed_id.ok()) {
    return parsed_id.status();
  } else {
    context.setNext(parsed_id->context());
    cdn_id = parsed_id->cdnId();
  }

  context.setNext(skipOptionalWhitespace(context));

  while (!context.atEnd()) {
    if (context.peek() != ';') {
      break;
    }
    context.increment();

    context.setNext(skipOptionalWhitespace(context));

    if (StatusOr<ParseContext> parameter = parseParameter(context); !parameter.ok()) {
      return parameter.status();
    } else {
      context.setNext(*parameter);
    }

    context.setNext(skipOptionalWhitespace(context));
  }

  return ParsedCdnInfo(context, cdn_id);
}

StatusOr<ParsedCdnInfoList> parseCdnInfoList(const ParseContext& input) {
  std::vector<absl::string_view> cdn_infos;
  ParseContext context = input;

  context.setNext(skipOptionalWhitespace(context));

  while (!context.atEnd()) {
    // Loop invariant: we're always at the beginning of a new element.

    if (context.peek() == ',') {
      // Empty element case
      context.increment();
      context.setNext(skipOptionalWhitespace(context));
      continue;
    }

    if (StatusOr<ParsedCdnInfo> parsed_cdn_info = parseCdnInfo(context); !parsed_cdn_info.ok()) {
      return parsed_cdn_info.status();
    } else {
      cdn_infos.push_back(parsed_cdn_info->cdnId());
      context.setNext(parsed_cdn_info->context());
    }

    context.setNext(skipOptionalWhitespace(context));

    if (context.atEnd()) {
      break;
    }

    if (context.peek() != ',') {
      return absl::InvalidArgumentError(absl::StrFormat("expected ',' at position %d; found '%c'",
                                                        context.next(), context.peek()));
    } else {
      context.increment();
    }

    context.setNext(skipOptionalWhitespace(context));
  }

  return ParsedCdnInfoList(context, std::move(cdn_infos));
}

} // namespace Parser
} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
