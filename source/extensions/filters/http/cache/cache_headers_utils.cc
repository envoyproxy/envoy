#include "extensions/filters/http/cache/cache_headers_utils.h"

#include <array>
#include <chrono>
#include <string>

#include "envoy/http/header_map.h"

#include "common/http/header_map_impl.h"
#include "common/http/header_utility.h"

#include "extensions/filters/http/cache/inline_headers_handles.h"

#include "absl/algorithm/container.h"
#include "absl/strings/ascii.h"
#include "absl/strings/numbers.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace Cache {

// Utility functions used in RequestCacheControl & ResponseCacheControl.
namespace {
// A directive with an invalid duration is ignored, the RFC does not specify a behavior:
// https://httpwg.org/specs/rfc7234.html#delta-seconds
OptionalDuration parseDuration(absl::string_view s) {
  OptionalDuration duration;
  // Strip quotation marks if any.
  if (s.size() > 1 && s.front() == '"' && s.back() == '"') {
    s = s.substr(1, s.size() - 2);
  }
  long num;
  if (absl::SimpleAtoi(s, &num) && num >= 0) {
    // s is a valid string of digits representing a positive number.
    duration = Seconds(num);
  }
  return duration;
}

inline std::pair<absl::string_view, absl::string_view>
separateDirectiveAndArgument(absl::string_view full_directive) {
  return absl::StrSplit(absl::StripAsciiWhitespace(full_directive), absl::MaxSplits('=', 1));
}
} // namespace

// The grammar for This Cache-Control header value should be:
// Cache-Control   = 1#cache-directive
// cache-directive = token [ "=" ( token / quoted-string ) ]
// token           = 1*tchar
// tchar           = "!" / "#" / "$" / "%" / "&" / "'" / "*" / "+"
//                 / "-" / "." / "^" / "_" / "`" / "|" / "~" / DIGIT / ALPHA
// quoted-string   = DQUOTE *( qdtext / quoted-pair ) DQUOTE
// qdtext          = HTAB / SP /%x21 / %x23-5B / %x5D-7E / obs-text
// obs-text        = %x80-FF
// quoted-pair     = "\" ( HTAB / SP / VCHAR / obs-text )
// VCHAR           =  %x21-7E  ; visible (printing) characters

// Multiple directives are comma separated according to:
// https://httpwg.org/specs/rfc7234.html#collected.abnf

RequestCacheControl::RequestCacheControl(absl::string_view cache_control_header) {
  const std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      must_validate_ = true;
    } else if (directive == "no-store") {
      no_store_ = true;
    } else if (directive == "no-transform") {
      no_transform_ = true;
    } else if (directive == "only-if-cached") {
      only_if_cached_ = true;
    } else if (directive == "max-age") {
      max_age_ = parseDuration(argument);
    } else if (directive == "min-fresh") {
      min_fresh_ = parseDuration(argument);
    } else if (directive == "max-stale") {
      max_stale_ = argument.empty() ? SystemTime::duration::max() : parseDuration(argument);
    }
  }
}

ResponseCacheControl::ResponseCacheControl(absl::string_view cache_control_header) {
  const std::vector<absl::string_view> directives = absl::StrSplit(cache_control_header, ',');

  for (auto full_directive : directives) {
    absl::string_view directive, argument;
    std::tie(directive, argument) = separateDirectiveAndArgument(full_directive);

    if (directive == "no-cache") {
      // If no-cache directive has arguments they are ignored - not handled.
      must_validate_ = true;
    } else if (directive == "must-revalidate" || directive == "proxy-revalidate") {
      no_stale_ = true;
    } else if (directive == "no-store" || directive == "private") {
      // If private directive has arguments they are ignored - not handled.
      no_store_ = true;
    } else if (directive == "no-transform") {
      no_transform_ = true;
    } else if (directive == "public") {
      is_public_ = true;
    } else if (directive == "s-maxage") {
      max_age_ = parseDuration(argument);
    } else if (!max_age_.has_value() && directive == "max-age") {
      max_age_ = parseDuration(argument);
    }
  }
}

bool operator==(const RequestCacheControl& lhs, const RequestCacheControl& rhs) {
  return (lhs.must_validate_ == rhs.must_validate_) && (lhs.no_store_ == rhs.no_store_) &&
         (lhs.no_transform_ == rhs.no_transform_) && (lhs.only_if_cached_ == rhs.only_if_cached_) &&
         (lhs.max_age_ == rhs.max_age_) && (lhs.min_fresh_ == rhs.min_fresh_) &&
         (lhs.max_stale_ == rhs.max_stale_);
}

bool operator==(const ResponseCacheControl& lhs, const ResponseCacheControl& rhs) {
  return (lhs.must_validate_ == rhs.must_validate_) && (lhs.no_store_ == rhs.no_store_) &&
         (lhs.no_transform_ == rhs.no_transform_) && (lhs.no_stale_ == rhs.no_stale_) &&
         (lhs.is_public_ == rhs.is_public_) && (lhs.max_age_ == rhs.max_age_);
}

SystemTime CacheHeadersUtils::httpTime(const Http::HeaderEntry* header_entry) {
  if (!header_entry) {
    return {};
  }
  absl::Time time;
  const std::string input(header_entry->value().getStringView());

  // Acceptable Date/Time Formats per:
  // https://tools.ietf.org/html/rfc7231#section-7.1.1.1
  //
  // Sun, 06 Nov 1994 08:49:37 GMT    ; IMF-fixdate.
  // Sunday, 06-Nov-94 08:49:37 GMT   ; obsolete RFC 850 format.
  // Sun Nov  6 08:49:37 1994         ; ANSI C's asctime() format.
  static const char* rfc7231_date_formats[] = {"%a, %d %b %Y %H:%M:%S GMT",
                                               "%A, %d-%b-%y %H:%M:%S GMT", "%a %b %e %H:%M:%S %Y"};

  for (const std::string& format : rfc7231_date_formats) {
    if (absl::ParseTime(format, input, &time, nullptr)) {
      return ToChronoTime(time);
    }
  }
  return {};
}

Seconds CacheHeadersUtils::calculateAge(const Http::ResponseHeaderMap& response_headers,
                                        const SystemTime response_time, const SystemTime now) {
  // Age headers calculations follow: https://httpwg.org/specs/rfc7234.html#age.calculations
  const SystemTime date_value = CacheHeadersUtils::httpTime(response_headers.Date());

  long age_value;
  const absl::string_view age_header = response_headers.getInlineValue(age_handle.handle());
  if (!absl::SimpleAtoi(age_header, &age_value)) {
    age_value = 0;
  }

  const SystemTime::duration apparent_age =
      std::max(SystemTime::duration(0), response_time - date_value);

  // Assumption: response_delay is negligible -> corrected_age_value = age_value.
  const SystemTime::duration corrected_age_value = Seconds(age_value);
  const SystemTime::duration corrected_initial_age = std::max(apparent_age, corrected_age_value);

  // Calculate current_age:
  const SystemTime::duration resident_time = now - response_time;
  const SystemTime::duration current_age = corrected_initial_age + resident_time;

  return std::chrono::duration_cast<Seconds>(current_age);
}

absl::optional<uint64_t> CacheHeadersUtils::readAndRemoveLeadingDigits(absl::string_view& str) {
  uint64_t val = 0;
  uint32_t bytes_consumed = 0;

  for (const char cur : str) {
    if (!absl::ascii_isdigit(cur)) {
      break;
    }
    uint64_t new_val = (val * 10) + (cur - '0');
    if (new_val / 8 < val) {
      // Overflow occurred
      return absl::nullopt;
    }
    val = new_val;
    ++bytes_consumed;
  }

  if (bytes_consumed) {
    // Consume some digits
    str.remove_prefix(bytes_consumed);
    return val;
  }
  return absl::nullopt;
}

void CacheHeadersUtils::getAllMatchingHeaderNames(
    const Http::HeaderMap& headers, const std::vector<Matchers::StringMatcherPtr>& ruleset,
    absl::flat_hash_set<absl::string_view>& out) {
  headers.iterate([&ruleset, &out](const Http::HeaderEntry& header) -> Http::HeaderMap::Iterate {
    absl::string_view header_name = header.key().getStringView();
    for (const auto& rule : ruleset) {
      if (rule->match(header_name)) {
        out.emplace(header_name);
        break;
      }
    }
    return Http::HeaderMap::Iterate::Continue;
  });
}

std::vector<std::string>
CacheHeadersUtils::parseCommaDelimitedList(const Http::HeaderMap::GetResult& entry) {
  if (entry.empty()) {
    return {};
  }

  // TODO(mattklein123): Consider multiple header values?
  std::vector<std::string> header_values = absl::StrSplit(entry[0]->value().getStringView(), ',');
  for (std::string& value : header_values) {
    // TODO(cbdm): Might be able to improve the performance here by using StringUtil::trim to
    // remove whitespace.
    absl::StripAsciiWhitespace(&value);
  }

  return header_values;
}

VaryHeader::VaryHeader(
    const Protobuf::RepeatedPtrField<envoy::type::matcher::v3::StringMatcher>& allow_list) {

  for (const auto& rule : allow_list) {
    allow_list_.emplace_back(std::make_unique<Matchers::StringMatcherImpl>(rule));
  }
}

bool VaryHeader::isAllowed(const Http::ResponseHeaderMap& headers) const {
  if (!VaryHeader::hasVary(headers)) {
    return true;
  }

  std::vector<std::string> varied_headers =
      CacheHeadersUtils::parseCommaDelimitedList(headers.get(Http::Headers::get().Vary));

  for (const std::string& header : varied_headers) {
    bool valid = false;

    // "Vary: *" should never be cached per:
    // https://tools.ietf.org/html/rfc7231#section-7.1.4
    if (header == "*") {
      return false;
    }

    for (const auto& rule : allow_list_) {
      if (rule->match(header)) {
        valid = true;
        break;
      }
    }

    if (!valid) {
      return false;
    }
  }

  return true;
}

bool VaryHeader::hasVary(const Http::ResponseHeaderMap& headers) {
  // TODO(mattklein123): Support multiple vary headers and/or just make the vary header inline.
  const auto vary_header = headers.get(Http::Headers::get().Vary);
  return !vary_header.empty() && !vary_header[0]->value().empty();
}

namespace {
// The separator characters are used to create the vary-key, and must be characters that are
// invalid to be inside values and header names. The chosen characters are invalid per:
// https://tools.ietf.org/html/rfc2616#section-4.2.

// Used to separate the values of different headers.
constexpr absl::string_view header_separator = "\n";
// Used to separate multiple values of a same header.
constexpr absl::string_view in_value_separator = "\r";
}; // namespace

std::string VaryHeader::createVaryKey(const Http::HeaderMap::GetResult& vary_header,
                                      const Http::RequestHeaderMap& entry_headers) {
  if (vary_header.empty()) {
    return "";
  }

  // TODO(mattklein123): Support multiple vary headers and/or just make the vary header inline.
  ASSERT(vary_header[0]->key() == "vary");

  std::string vary_key = "vary-key\n";

  for (const std::string& header : CacheHeadersUtils::parseCommaDelimitedList(vary_header)) {
    // TODO(cbdm): Can add some bucketing logic here based on header. For example, we could
    // normalize the values for accept-language by making all of {en-CA, en-GB, en-US} into
    // "en". This way we would not need to store multiple versions of the same payload, and any
    // of those values would find the payload in the requested language. Another example would be to
    // bucket UserAgent values into android/ios/desktop; UserAgent::initializeFromHeaders tries to
    // do that normalization and could be used as an inspiration for some bucketing configuration.
    // The config should enable and control the bucketing wanted.
    const auto all_values = Http::HeaderUtility::getAllOfHeaderAsString(
        entry_headers, Http::LowerCaseString(header), in_value_separator);
    absl::StrAppend(&vary_key, header, in_value_separator,
                    all_values.result().has_value() ? all_values.result().value() : "",
                    header_separator);
  }

  return vary_key;
}

Http::RequestHeaderMapPtr
VaryHeader::possibleVariedHeaders(const Http::RequestHeaderMap& request_headers) const {
  Http::RequestHeaderMapPtr possible_headers =
      Http::createHeaderMap<Http::RequestHeaderMapImpl>({});

  absl::flat_hash_set<absl::string_view> header_names;
  CacheHeadersUtils::getAllMatchingHeaderNames(request_headers, allow_list_, header_names);

  for (const absl::string_view& header : header_names) {
    const auto lower_case_header = Http::LowerCaseString(std::string{header});
    const auto value = request_headers.get(lower_case_header);
    for (size_t i = 0; i < value.size(); i++) {
      possible_headers->addCopy(lower_case_header, value[i]->value().getStringView());
    }
  }

  return possible_headers;
}

} // namespace Cache
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
