#include "common/common/matchers.h"

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/header_map.h"

#include "common/config/metadata.h"
#include "common/config/rds_json.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Matchers {

ValueMatcherConstSharedPtr ValueMatcher::create(const envoy::type::matcher::ValueMatcher& v) {
  switch (v.match_pattern_case()) {
  case envoy::type::matcher::ValueMatcher::kNullMatch:
    return std::make_shared<const NullMatcher>();
  case envoy::type::matcher::ValueMatcher::kDoubleMatch:
    return std::make_shared<const DoubleMatcher>(v.double_match());
  case envoy::type::matcher::ValueMatcher::kStringMatch:
    return std::make_shared<const StringMatcher>(v.string_match());
  case envoy::type::matcher::ValueMatcher::kBoolMatch:
    return std::make_shared<const BoolMatcher>(v.bool_match());
  case envoy::type::matcher::ValueMatcher::kPresentMatch:
    return std::make_shared<const PresentMatcher>(v.present_match());
  case envoy::type::matcher::ValueMatcher::kListMatch:
    return std::make_shared<const ListMatcher>(v.list_match());
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool NullMatcher::match(const ProtobufWkt::Value& value) const {
  return value.kind_case() == ProtobufWkt::Value::kNullValue;
}

bool BoolMatcher::match(const ProtobufWkt::Value& value) const {
  return value.kind_case() == ProtobufWkt::Value::kBoolValue && matcher_ == value.bool_value();
}

bool PresentMatcher::match(const ProtobufWkt::Value& value) const {
  return matcher_ && value.kind_case() != ProtobufWkt::Value::KIND_NOT_SET;
}

bool DoubleMatcher::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kNumberValue) {
    return false;
  }

  const double v = value.number_value();
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::DoubleMatcher::kRange:
    return matcher_.range().start() <= v && v < matcher_.range().end();
  case envoy::type::matcher::DoubleMatcher::kExact:
    return matcher_.exact() == v;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  };
}

bool StringMatcher::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kStringValue) {
    return false;
  }

  const std::string& s = value.string_value();
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::StringMatcher::kExact:
    return matcher_.exact() == s;
  case envoy::type::matcher::StringMatcher::kPrefix:
    return absl::StartsWith(s, matcher_.prefix());
  case envoy::type::matcher::StringMatcher::kSuffix:
    return absl::EndsWith(s, matcher_.suffix());
  case envoy::type::matcher::StringMatcher::kRegex:
    return std::regex_match(s, regex_);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

ListMatcher::ListMatcher(const envoy::type::matcher::ListMatcher& matcher) : matcher_(matcher) {
  ASSERT(matcher_.match_pattern_case() == envoy::type::matcher::ListMatcher::kOneOf);

  oneof_value_matcher_ = ValueMatcher::create(matcher_.one_of());
}

bool ListMatcher::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kListValue) {
    return false;
  }

  if (oneof_value_matcher_) {
    for (const auto& lv : value.list_value().values()) {
      if (oneof_value_matcher_->match(lv)) {
        return true;
      }
    }
  }
  return false;
}

MetadataMatcher::MetadataMatcher(const envoy::type::matcher::MetadataMatcher& matcher)
    : matcher_(matcher) {
  for (const auto& seg : matcher.path()) {
    path_.push_back(seg.key());
  }
  const auto& v = matcher_.value();
  value_matcher_ = ValueMatcher::create(v);
}

bool MetadataMatcher::match(const envoy::api::v2::core::Metadata& metadata) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(metadata, matcher_.filter(), path_);
  return value_matcher_ && value_matcher_->match(value);
}

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
HeaderUtility::HeaderData::HeaderData(const envoy::type::matcher::HeaderMatcher& config)
    : name_(config.name()), invert_match_(config.invert_match()) {
  switch (config.header_match_specifier_case()) {
  case envoy::type::matcher::HeaderMatcher::kExactMatch:
    header_match_type_ = HeaderMatchType::Value;
    value_ = config.exact_match();
    break;
  case envoy::type::matcher::HeaderMatcher::kRegexMatch:
    header_match_type_ = HeaderMatchType::Regex;
    regex_pattern_ = RegexUtil::parseRegex(config.regex_match());
    break;
  case envoy::type::matcher::HeaderMatcher::kRangeMatch:
    header_match_type_ = HeaderMatchType::Range;
    range_.set_start(config.range_match().start());
    range_.set_end(config.range_match().end());
    break;
  case envoy::type::matcher::HeaderMatcher::kPresentMatch:
    header_match_type_ = HeaderMatchType::Present;
    break;
  case envoy::type::matcher::HeaderMatcher::kPrefixMatch:
    header_match_type_ = HeaderMatchType::Prefix;
    value_ = config.prefix_match();
    break;
  case envoy::type::matcher::HeaderMatcher::kSuffixMatch:
    header_match_type_ = HeaderMatchType::Suffix;
    value_ = config.suffix_match();
    break;
  case envoy::type::matcher::HeaderMatcher::HEADER_MATCH_SPECIFIER_NOT_SET:
    FALLTHRU;
  default:
    header_match_type_ = HeaderMatchType::Present;
    break;
  }
}

HeaderUtility::HeaderData::HeaderData(const Json::Object& config)
    : HeaderData([&config] {
        envoy::type::matcher::HeaderMatcher header_matcher;
        Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
        return header_matcher;
      }()) {}

bool HeaderUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const std::vector<HeaderData>& config_headers) {
  // TODO (rodaine): Should this really allow empty headers to always match?
  if (!config_headers.empty()) {
    for (const HeaderData& cfg_header_data : config_headers) {
      if (!matchHeaders(request_headers, cfg_header_data)) {
        return false;
      }
    }
  }

  return true;
}

bool HeaderUtility::matchHeaders(const Http::HeaderMap& request_headers,
                                 const HeaderData& header_data) {
  const Http::HeaderEntry* header = request_headers.get(header_data.name_);

  if (header == nullptr) {
    return header_data.invert_match_ && header_data.header_match_type_ == HeaderMatchType::Present;
  }

  bool match;
  switch (header_data.header_match_type_) {
  case HeaderMatchType::Value:
    match = header_data.value_.empty() || header->value() == header_data.value_.c_str();
    break;
  case HeaderMatchType::Regex:
    match = std::regex_match(header->value().c_str(), header_data.regex_pattern_);
    break;
  case HeaderMatchType::Range: {
    int64_t header_value = 0;
    match = StringUtil::atol(header->value().c_str(), header_value, 10) &&
            header_value >= header_data.range_.start() && header_value < header_data.range_.end();
    break;
  }
  case HeaderMatchType::Present:
    match = true;
    break;
  case HeaderMatchType::Prefix:
    match = absl::StartsWith(header->value().getStringView(), header_data.value_);
    break;
  case HeaderMatchType::Suffix:
    match = absl::EndsWith(header->value().getStringView(), header_data.value_);
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }

  return match != header_data.invert_match_;
}

void HeaderUtility::addHeaders(Http::HeaderMap& headers, const Http::HeaderMap& headers_to_add) {
  headers_to_add.iterate(
      [](const Http::HeaderEntry& header, void* context) -> Http::HeaderMap::Iterate {
        const Http::LowerCaseString& k = Http::LowerCaseString(header.key().c_str());
        static_cast<Http::HeaderMap*>(context)->addCopy(k, header.value().getStringView());
        return Http::HeaderMap::Iterate::Continue;
      },
      &headers);
}

} // namespace Matchers
} // namespace Envoy
