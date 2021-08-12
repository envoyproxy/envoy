#pragma once

#include <iostream>

#include "source/common/common/statusor.h"

#include "absl/strings/str_join.h"
#include "absl/strings/string_view.h"

// This file defines a parser for the CDN-Loop header value.
//
// RFC 8586 Section 2 defined the CDN-Loop header as:
//
//   CDN-Loop  = #cdn-info
//   cdn-info  = cdn-id *( OWS ";" OWS parameter )
//   cdn-id    = ( uri-host [ ":" port ] ) / pseudonym
//   pseudonym = token
//
// Each of those productions rely on definitions in RFC 3986, RFC 5234, RFC
// 7230, and RFC 7231. Their use is noted in the individual parse functions.
//
// The parser is a top-down combined parser and lexer that implements just
// enough of the RFC spec to make it possible count the number of times a
// particular CDN value appears. The main differences between the RFC's grammar
// and the parser defined here are:
//
// 1. the parser has a more lax interpretation of what's a valid uri-host. See
//     ParseCdnId for details.
//
// 2. the parser allows leading and trailing whitespace around the header
//     value. See ParseCdnInfoList for details.
//
// Each parse function takes as input a ParseContext that tells the
// function where to start. Parse functions that just need to parse a portion
// of the CDN-Loop header, but don't need to return a value, should return a
// ParseContext pointing to the next character to parse. Parse functions that
// need to return a value should return something that contains a ParseContext.
//
// Parse functions that can fail (most of them!) wrap their return value in an
// Envoy::StatusOr.
//
// In the interest of performance, this parser works with string_views and
// references instead of copying std::strings. The string_view passed into the
// ParseContext of a parse function must outlive the return value of the
// function.

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace CdnLoop {
namespace Parser {

// A ParseContext contains the state of the recursive descent parser and some
// helper methods.
class ParseContext {
public:
  ParseContext(absl::string_view value) : value_(value), next_(0) {}
  ParseContext(absl::string_view value, absl::string_view::size_type next)
      : value_(value), next_(next) {}

  // Returns true if we have reached the end of value.
  constexpr bool atEnd() const { return value_.length() <= next_; }

  // Returns the value we're parsing
  constexpr absl::string_view value() const { return value_; }

  // Returns the position of the next character to process.
  constexpr absl::string_view::size_type next() const { return next_; }

  // Returns the character at next.
  //
  // REQUIRES: !at_end()
  constexpr char peek() const { return value_[next_]; }

  // Moves to the next character.
  constexpr void increment() { ++next_; }

  // Sets next from another context.
  constexpr void setNext(const ParseContext& other) { next_ = other.next_; }

  constexpr bool operator==(const ParseContext& other) const {
    return value_ == other.value_ && next_ == other.next_;
  }
  constexpr bool operator!=(const ParseContext& other) const { return !(*this == other); }

  friend std::ostream& operator<<(std::ostream& os, ParseContext arg) {
    return os << "ParseContext{next=" << arg.next_ << "}";
  }

private:
  // The item we're parsing.
  const absl::string_view value_;

  // A pointer to the next value we should parse.
  absl::string_view::size_type next_;
};

// A ParsedCdnId holds an extracted CDN-Loop cdn-id.
class ParsedCdnId {
public:
  ParsedCdnId(ParseContext context, absl::string_view cdn_id)
      : context_(context), cdn_id_(cdn_id) {}

  ParseContext context() const { return context_; }

  absl::string_view cdnId() const { return cdn_id_; }

  constexpr bool operator==(const ParsedCdnId& other) const {
    return context_ == other.context_ && cdn_id_ == other.cdn_id_;
  }
  constexpr bool operator!=(const ParsedCdnId& other) const { return !(*this == other); }

  friend std::ostream& operator<<(std::ostream& os, ParsedCdnId arg) {
    return os << "ParsedCdnId{context=" << arg.context_ << ", cdn_id=" << arg.cdn_id_ << "}";
  }

private:
  ParseContext context_;
  absl::string_view cdn_id_;
};

// A ParsedCdnInfo holds the extracted cdn-id after parsing an entire cdn-info.
struct ParsedCdnInfo {
  ParsedCdnInfo(ParseContext context, absl::string_view cdn_id)
      : context_(context), cdn_id_(cdn_id) {}

  ParseContext context() const { return context_; }

  absl::string_view cdnId() const { return cdn_id_; }

  constexpr bool operator==(const ParsedCdnInfo& other) const {
    return context_ == other.context_ && cdn_id_ == other.cdn_id_;
  }
  constexpr bool operator!=(const ParsedCdnInfo& other) const { return !(*this == other); }

  friend std::ostream& operator<<(std::ostream& os, ParsedCdnInfo arg) {
    return os << "ParsedCdnInfo{context=" << arg.context_ << ", cdn_id=" << arg.cdn_id_ << "}";
  }

private:
  ParseContext context_;
  absl::string_view cdn_id_;
};

// A ParsedCdnInfoList contains list of cdn-ids after parsing the entire
// CDN-Loop production.
struct ParsedCdnInfoList {
  ParsedCdnInfoList(ParseContext context, std::vector<absl::string_view> cdn_ids)
      : context_(context), cdn_ids_(std::move(cdn_ids)) {}

  constexpr const std::vector<absl::string_view>& cdnIds() { return cdn_ids_; }

  constexpr bool operator==(const ParsedCdnInfoList& other) const {
    return context_ == other.context_ && cdn_ids_ == other.cdn_ids_;
  }
  constexpr bool operator!=(const ParsedCdnInfoList& other) const { return !(*this == other); }

  friend std::ostream& operator<<(std::ostream& os, ParsedCdnInfoList arg) {
    return os << "ParsedCdnInfoList{context=" << arg.context_ << ", cdn_ids=["
              << absl::StrJoin(arg.cdn_ids_, ", ") << "]}";
  }

private:
  ParseContext context_;
  std::vector<absl::string_view> cdn_ids_;
};

// Skips optional whitespace according to RFC 7230 Section 3.2.3.
//
// OWS  = *( SP / HTAB )
//
// Since this is completely optional, there's no way this call can fail.
ParseContext skipOptionalWhitespace(const ParseContext& input);

// Parses a quoted-pair according to RFC 7230 Section 3.2.6.
//
// quoted-pair    = "\" ( HTAB / SP / VCHAR / obs-text )
StatusOr<ParseContext> parseQuotedPair(const ParseContext& input);

// Parses a quoted-string according to RFC 7230 Section 3.2.6.
//
// quoted-string  = DQUOTE *( qdtext / quoted-pair ) DQUOTE
// qdtext         = HTAB / SP / %x21 / %x23-5B / %x5D-7E / obs-text
// obs-text       = %x80-FF
//
// quoted-pair    = "\" ( HTAB / SP / VCHAR / obs-text )
StatusOr<ParseContext> parseQuotedString(const ParseContext& input);

// Parses a token according to RFC 7320 Section 3.2.6.
//
// token          = 1*tchar
//
// tchar          = "!" / "#" / "$" / "%" / "&" / "'" / "*"
//                / "+" / "-" / "." / "^" / "_" / "`" / "|" / "~"
//                / DIGIT / ALPHA
//                ; any VCHAR, except delimiters
//
// According to RFC 5234 Appendix B.1:
//
// ALPHA          =  %x41-5A / %x61-7A   ; A-Z / a-z
//
// DIGIT          =  %x30-39
StatusOr<ParseContext> parseToken(const ParseContext& input);

// Parses something that looks like an IPv6 address literal.
//
// A proper IPv6 address literal is defined in RFC 3986, Section 3.2.2 as part
// of the host rule. We're going to allow something simpler:
//
// plausible-ipv6 = "[" *( HEXDIGIT | "." | ":" ) "]
// HEXDIGIT = DIGIT | %x41-46 | %x61-66 ; 0-9 | A-F | a-f
//
// Compared to the real rule, our rule:
//
// - allows lower-case hex digits
// - allows address sections with more than 4 hex digits in a row
// - allows embedded IPv4 addresses multiple times rather than just at the end.
StatusOr<ParseContext> parsePlausibleIpV6(const ParseContext& input);

// Parses a cdn-id in a lax way.
//
// According to to RFC 8586 Section 2, the cdn-id is:
//
// cdn-id    = ( uri-host [ ":" port ] ) / pseudonym
// pseudonym = token
//
// The uri-host portion of the cdn-id is the "host" rule from RFC 3986 Section
// 3.2.2. Parsing the host rule is remarkably difficult because the host rule
// tries to parse exactly valid IP addresses (e.g., disallowing values greater
// than 255 in an IPv4 address or only allowing one instance of "::" in IPv6
// addresses) and needs to deal with % escaping in names.
//
// Worse, the uri-host reg-name rule admits ',' and ';' as members of sub-delim
// rule, making parsing ambiguous in some cases! RFC 3986 does this in order to
// be "future-proof" for naming schemes we haven't dreamed up yet. RFC 8586
// says that if a CDN uses a uri-host as its cdn-id, the uri-host must be a
// "hostname under its control". The only global naming system we have is DNS,
// so the only really valid reg-name an Internet-facing Envoy should see is a
// DNS name.
//
// Luckily, the token rule more or less covers the uri-host rule for DNS names
// and for IPv4 addresses. We just a new rule to parse IPv6 addresses. See
// ParsePlausibleIpV6 for the rule we'll follow.
//
// The definition of port comes from RFC 3986 Section
// 3.2.3 as:
//
// port        = *DIGIT
//
// In other words, any number of digits is allowed.
//
// In all, this function will parse cdn-id as:
//
// cdn-id = ( plausible-ipv6-address / token ) [ ":" *DIGIT ]
StatusOr<ParsedCdnId> parseCdnId(const ParseContext& input);

// Parses a parameter according RFC 7231 Appendix D.
//
// parameter = token "=" ( token / quoted-string )
StatusOr<ParseContext> parseParameter(const ParseContext& input);

// Parses a cdn-info according to RFC 8586 Section 2.
//
// cdn-info  = cdn-id *( OWS ";" OWS parameter )
StatusOr<ParsedCdnInfo> parseCdnInfo(const ParseContext& input);

// Parses the top-level cdn-info according to RFC 8586 Section 2.
//
// CDN-Loop  = #cdn-info
//
// The # rule is defined by RFC 7230 Section 7. The # is different for senders
// and recipients. We're a recipient, so:
//
//   For compatibility with legacy list rules, a recipient MUST parse and
//   ignore a reasonable number of empty list elements: enough to handle
//   common mistakes by senders that merge values, but not so much that
//   they could be used as a denial-of-service mechanism. In other words,
//   a recipient MUST accept lists that satisfy the following syntax:
//
//     #element => [ ( "," / element ) *( OWS "," [ OWS element ] ) ]
//
//     1#element => *( "," OWS ) element *( OWS "," [ OWS element ] )
//
//   Empty elements do not contribute to the count of elements present.
//
// Since #cdn-info uses the #element form, we have to parse (but not count)
// blank entries.
//
// In a divergence with the RFC's grammar, this function will also ignore
// leading and trailing OWS. This function expects to consume the entire input
// and will return an error if there is something it cannot parse.
StatusOr<ParsedCdnInfoList> parseCdnInfoList(const ParseContext& input);

} // namespace Parser
} // namespace CdnLoop
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
