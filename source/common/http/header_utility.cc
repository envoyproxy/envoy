#include "common/http/header_utility.h"

#include "common/common/utility.h"
#include "common/config/rds_json.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "absl/strings/str_join.h"
#include "absl/strings/str_split.h"

namespace Envoy {
namespace Http {

// HeaderMatcher will consist of:
//   header_match_specifier which can be any one of exact_match, regex_match, range_match,
//   present_match, prefix_match or suffix_match.
//   Each of these also can be inverted with the invert_match option.
//   Absence of these options implies empty header value match based on header presence.
//   a.exact_match: value will be used for exact string matching.
//   b.regex_match: Match will succeed if header value matches the value specified here.
//   c.range_match: Match will succeed if header value lies within the range specified
//     here, using half open interval semantics [start,end).
//   d.present_match: Match will succeed if the header is present.
//   f.prefix_match: Match will succeed if header value matches the prefix value specified here.
//   g.suffix_match: Match will succeed if header value matches the suffix value specified here.
HeaderUtility::HeaderData::HeaderData(const envoy::api::v2::route::HeaderMatcher& config)
    : name_(config.name()), invert_match_(config.invert_match()) {
  switch (config.header_match_specifier_case()) {
  case envoy::api::v2::route::HeaderMatcher::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::api::v2::route::HeaderMatcher::kRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_pattern_ = RegexUtil::parseRegex(config.regex_match());
    break;
  case envoy::api::v2::route::HeaderMatcher::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::api::v2::route::HeaderMatcher::kPresentMatch:
    header_match_type_ = HeaderMatchType::Present;
    break;
  case envoy::api::v2::route::HeaderMatcher::kPrefixMatch:
    header_match_type_ = HeaderMatchType::Prefix;
    value_ = config.prefix_match();
    break;
  case envoy::api::v2::route::HeaderMatcher::kSuffixMatch:
    header_match_type_ = HeaderMatchType::Suffix;
    value_ = config.suffix_match();
    break;
  case envoy::api::v2::route::HeaderMatcher::HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    header_match_type_ = HeaderMatchType::Present;
    break;
  }
}

HeaderUtility::HeaderData::HeaderData(const Json::Object& config)
    : HeaderData([&config] {
        envoy::api::v2::route::HeaderMatcher header_matcher;
        Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
        return header_matcher;
      }()) {}

std::string removeDotSegments(absl::string_view input) {
  std::vector<absl::string_view> output;
  while (!input.empty()) {
    // A. If the input buffer begins with a prefix of "../" or "./",
    // then remove that prefix from the input buffer;
    if (absl::StartsWith(input, "../")) {
      input.remove_prefix(3);
      continue;
    }
    if (absl::StartsWith(input, "./")) {
      input.remove_prefix(2);
      continue;
    }

    // B. if the input buffer begins with a prefix of "/./" or "/.",
    // where "." is a complete path segment, then replace that
    // prefix with "/" in the input buffer;
    if (absl::StartsWith(input, "/./")) {
      input.remove_prefix(2);
      continue;
    }
    if (input == "/.") {
      output.push_back("/");
      break;
    }

    // C. if the input buffer begins with a prefix of "/../" or "/..",
    // where ".." is a complete path segment, then replace that
    // prefix with "/" in the input buffer and remove the last
    // segment and its preceding "/" (if any) from the output
    // buffer;
    if (absl::StartsWith(input, "/../")) {
      if (!output.empty()) {
        output.pop_back();
      }
      input.remove_prefix(3);
      continue;
    }
    if (input == "/..") {
      if (!output.empty()) {
        output.pop_back();
      }
      output.push_back("/");
      break;
    }

    // D. if the input buffer consists only of "." or "..", then remove
    // that from the input buffer;
    if (input == "." || input == "..") {
      break;
    }

    // E. move the first path segment in the input buffer to the end of
    // the output buffer, including the initial "/" character (if
    // any) and any subsequent characters up to, but not including,
    // the next "/" character or the end of the input buffer.
    auto j = input.find("/", 1);
    auto seg = input.substr(0, j);
    output.push_back(seg);
    input.remove_prefix(seg.length());
  }
  return absl::StrJoin(output, "");
}

bool HeaderUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers,
                                 const MatchOption& match_option) {
  // No headers to match is considered a match.
  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      if (!matchHeaders(request_headers, cfg_header_data, match_option)) {
        return false;
      }
    }
  }

  return true;
}

bool HeaderUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const HeaderData& header_data, const MatchOption& match_option) {
  const Http::HeaderEntry* header = request_headers.get(header_data.name_);

  if (header == nullptr) {
    return header_data.invert_match_ && header_data.header_match_type_ == HeaderMatchType::Present;
  }

  auto value = header->value().getStringView();
  std::string path_removed_dots;
  if (match_option.remove_dot_segments_in_path && header->key().getStringView() == ":path") {
    path_removed_dots = removeDotSegments(value);
    value = path_removed_dots;
  }

  bool match;
  switch (header_data.header_match_type_) {
  case HeaderMatchType::Value:
    match = header_data.value_.empty() || value == header_data.value_.c_str();
    break;
  case HeaderMatchType::Regex:
    match = std::regex_match(value.data(), header_data.regex_pattern_);
    break;
  case HeaderMatchType::Range: {
    int64_t header_value = 0;
    match = StringUtil::atoll(header->value().c_str(), header_value, 10) &&
            header_value >= header_data.range_.start() && header_value < header_data.range_.end();
    break;
  }
  case HeaderMatchType::Present:
    match = true;
    break;
  case HeaderMatchType::Prefix:
    match = absl::StartsWith(value, header_data.value_);
    break;
  case HeaderMatchType::Suffix:
    match = absl::EndsWith(value, header_data.value_);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return match != header_data.invert_match_;
}

void HeaderUtility::addHeaders(Http::HeaderMap& headers, const Http::HeaderMap& headers_to_add) {
  headers_to_add.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        Http::HeaderString k;
        k.setCopy(header.key().c_str(), header.key().size());
        Http::HeaderString v;
        v.setCopy(header.value().c_str(), header.value().size());
        static_cast<Http::HeaderMapImpl*>(context)->addViaMove(std::move(k), std::move(v));
        return Http::HeaderMap::Iterate::Continue;
      },
      &headers);
}

} // namespace Http
} // namespace Envoy
