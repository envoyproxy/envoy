#include "common/http/header_utility.h"

#include "envoy/config/route/v3/route_components.pb.h"

#include "common/common/regex.h"
#include "common/common/utility.h"
#include "common/http/header_map_impl.h"
#include "common/protobuf/utility.h"
#include "common/runtime/runtime_impl.h"

#include "absl/strings/match.h"
#include "nghttp2/nghttp2.h"

namespace Envoy {
namespace Http {

struct SharedResponseCodeDetailsValues {
  const absl::string_view InvalidAuthority = "http.invalid_authority";
  const absl::string_view ConnectUnsupported = "http.connect_not_supported";
};

using SharedResponseCodeDetails = ConstSingleton<SharedResponseCodeDetailsValues>;

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
HeaderUtility::HeaderData::HeaderData(const envoy::config::route::v3::HeaderMatcher& config)
    : name_(config.name()), invert_match_(config.invert_match()) {
  switch (config.header_match_specifier_case()) {
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      kHiddenEnvoyDeprecatedRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_ = Regex::Utility::parseStdRegexAsCompiledMatcher(
        config.hidden_envoy_deprecated_regex_match());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSafeRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_ = Regex::Utility::parseRegex(config.safe_regex_match());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPresentMatch:
    header_match_type_ = HeaderMatchType::Present;
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kPrefixMatch:
    header_match_type_ = HeaderMatchType::Prefix;
    value_ = config.prefix_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::kSuffixMatch:
    header_match_type_ = HeaderMatchType::Suffix;
    value_ = config.suffix_match();
    break;
  case envoy::config::route::v3::HeaderMatcher::HeaderMatchSpecifierCase::
      HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    header_match_type_ = HeaderMatchType::Present;
    break;
  }
}

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

HeaderUtility::GetAllOfHeaderAsStringResult
HeaderUtility::getAllOfHeaderAsString(const HeaderMap& headers, const Http::LowerCaseString& key) {
  GetAllOfHeaderAsStringResult result;
  const auto header_value = headers.getAll(key);

  if (header_value.empty()) {
    // Empty for clarity. Avoid handling the empty case in the block below if the runtime feature
    // is disabled.
  } else if (header_value.size() == 1 ||
             !Runtime::runtimeFeatureEnabled(
                 "envoy.reloadable_features.http_match_on_all_headers")) {
    result.result_ = header_value[0]->value().getStringView();
  } else {
    // In this case we concatenate all found headers using a ',' delimiter before performing the
    // final match. We use an InlinedVector of absl::string_view to invoke the optimized join
    // algorithm. This requires a copying phase before we invoke join. The 3 used as the inline
    // size has been arbitrarily chosen.
    // TODO(mattklein123): Do we need to normalize any whitespace here?
    absl::InlinedVector<absl::string_view, 3> string_view_vector;
    string_view_vector.reserve(header_value.size());
    for (size_t i = 0; i < header_value.size(); i++) {
      string_view_vector.push_back(header_value[i]->value().getStringView());
    }
    result.result_backing_string_ = absl::StrJoin(string_view_vector, ",");
  }

  return result;
}

bool HeaderUtility::matchHeaders(const HeaderMap& request_headers, const HeaderData& header_data) {
  const auto header_value = getAllOfHeaderAsString(request_headers, header_data.name_);

  if (!header_value.result().has_value()) {
    return header_data.invert_match_ && header_data.header_match_type_ == HeaderMatchType::Present;
  }

  bool match;
  switch (header_data.header_match_type_) {
  case HeaderMatchType::Value:
    match = header_data.value_.empty() || header_value.result().value() == header_data.value_;
    break;
  case HeaderMatchType::Regex:
    match = header_data.regex_->match(header_value.result().value());
    break;
  case HeaderMatchType::Range: {
    int64_t header_int_value = 0;
    match = absl::SimpleAtoi(header_value.result().value(), &header_int_value) &&
            header_int_value >= header_data.range_.start() &&
            header_int_value < header_data.range_.end();
    break;
  }
  case HeaderMatchType::Present:
    match = true;
    break;
  case HeaderMatchType::Prefix:
    match = absl::StartsWith(header_value.result().value(), header_data.value_);
    break;
  case HeaderMatchType::Suffix:
    match = absl::EndsWith(header_value.result().value(), header_data.value_);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return match != header_data.invert_match_;
}

bool HeaderUtility::headerValueIsValid(const absl::string_view header_value) {
  return nghttp2_check_header_value(reinterpret_cast<const uint8_t*>(header_value.data()),
                                    header_value.size()) != 0;
}

bool HeaderUtility::headerNameContainsUnderscore(const absl::string_view header_name) {
  return header_name.find('_') != absl::string_view::npos;
}

bool HeaderUtility::authorityIsValid(const absl::string_view header_value) {
  return nghttp2_check_authority(reinterpret_cast<const uint8_t*>(header_value.data()),
                                 header_value.size()) != 0;
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

absl::optional<std::reference_wrapper<const absl::string_view>>
HeaderUtility::requestHeadersValid(const HeaderMap& headers) {
  // Make sure the host is valid.
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_authority_validation") &&
      headers.Host() && !HeaderUtility::authorityIsValid(headers.Host()->value().getStringView())) {
    return SharedResponseCodeDetails::get().InvalidAuthority;
  }
  if (Runtime::runtimeFeatureEnabled("envoy.reloadable_features.strict_method_validation") &&
      headers.Method() &&
      Http::Headers::get().MethodValues.Connect == headers.Method()->value().getStringView()) {
    return SharedResponseCodeDetails::get().ConnectUnsupported;
  }
  return absl::nullopt;
}

} // namespace Http
} // namespace Envoy
