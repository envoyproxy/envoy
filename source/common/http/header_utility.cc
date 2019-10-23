#include "common/http/header_utility.h"

#include "common/common/regex.h"
#include "common/common/utility.h"
#include "common/config/rds_json.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"

#include "absl/strings/match.h"
#include "nghttp2/nghttp2.h"

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
    regex_ = Regex::Utility::parseStdRegexAsCompiledMatcher(config.regex_match());
    break;
  case envoy::api::v2::route::HeaderMatcher::kSafeRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_ = Regex::Utility::parseRegex(config.safe_regex_match());
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

void HeaderUtility::getAllOfHeader(const HeaderMap& headers, absl::string_view key,
                                   std::vector<absl::string_view>& out) {
  auto args = std::make_pair(LowerCaseString(std::string(key)), &out);

  headers.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        auto key_ret =
            static_cast<std::pair<LowerCaseString, std::vector<absl::string_view>*>*>(context);
        if (header.key() == key_ret->first.get().c_str()) {
          key_ret->second->emplace_back(header.value().getStringView());
        }
        return HeaderMap::Iterate::Continue;
      },
      &args);
}

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers,
                                 const std::vector<HeaderDataPtr>& config_headers) {
  // No headers to match is considered a match.
  if (!config_headers.empty()) {
    for (const HeaderDataPtr& cfg_header_data : config_headers) {
      if (!matchHeaders(request_headers, *cfg_header_data)) {
        return false;
      }
    }
  }

  return true;
}

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers, const HeaderData& header_data) {
  const HeaderEntry* header = request_headers.get(header_data.name_);

  if (header == nullptr) {
    return header_data.invert_match_ && header_data.header_match_type_ == HeaderMatchType::Present;
  }

  bool match;
  const absl::string_view header_view = header->value().getStringView();
  switch (header_data.header_match_type_) {
  case HeaderMatchType::Value:
    match = header_data.value_.empty() || header_view == header_data.value_;
    break;
  case HeaderMatchType::Regex:
    match = header_data.regex_->match(header_view);
    break;
  case HeaderMatchType::Range: {
    int64_t header_value = 0;
    match = absl::SimpleAtoi(header_view, &header_value) &&
            header_value >= header_data.range_.start() && header_value < header_data.range_.end();
    break;
  }
  case HeaderMatchType::Present:
    match = true;
    break;
  case HeaderMatchType::Prefix:
    match = absl::StartsWith(header_view, header_data.value_);
    break;
  case HeaderMatchType::Suffix:
    match = absl::EndsWith(header_view, header_data.value_);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return match != header_data.invert_match_;
}

bool HeaderUtility::headerIsValid(const absl::string_view header_value) {
  return (nghttp2_check_header_value(reinterpret_cast<const uint8_t*>(header_value.data()),
                                     header_value.size()) != 0);
}

void HeaderUtility::addHeaders(HeaderMap& headers, const HeaderMap& headers_to_add) {
  headers_to_add.iterate(
      [](const HeaderEntry& header, void* context) -> HeaderMap::Iterate {
        HeaderString k;
        k.setCopy(header.key().getStringView());
        HeaderString v;
        v.setCopy(header.value().getStringView());
        static_cast<HeaderMapImpl*>(context)->addViaMove(std::move(k), std::move(v));
        return HeaderMap::Iterate::Continue;
      },
      &headers);
}

bool HeaderUtility::isEnvoyInternalRequest(const HeaderMap& headers) {
  const HeaderEntry* internal_request_header = headers.EnvoyInternalRequest();
  return internal_request_header != nullptr &&
         internal_request_header->value() == Headers::get().EnvoyInternalRequestValues.True;
}

} // namespace Http
} // namespace Envoy
