#include "common/common/matchers.h"

#include "envoy/api/v2/core/base.pb.h"

#include "common/common/regex.h"
#include "common/config/metadata.h"
#include "common/http/path_utility.h"

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
    return std::make_shared<const StringMatcherImpl>(v.string_match());
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

StringMatcherImpl::StringMatcherImpl(const envoy::type::matcher::StringMatcher& matcher)
    : matcher_(matcher) {
  if (matcher.match_pattern_case() == envoy::type::matcher::StringMatcher::kRegex) {
    if (matcher.ignore_case()) {
      throw EnvoyException("ignore_case has no effect for regex.");
    }
    regex_ = Regex::Utility::parseStdRegexAsCompiledMatcher(matcher_.regex());
  } else if (matcher.match_pattern_case() == envoy::type::matcher::StringMatcher::kSafeRegex) {
    if (matcher.ignore_case()) {
      throw EnvoyException("ignore_case has no effect for safe_regex.");
    }
    regex_ = Regex::Utility::parseRegex(matcher_.safe_regex());
  }
}

bool StringMatcherImpl::match(const ProtobufWkt::Value& value) const {
  if (value.kind_case() != ProtobufWkt::Value::kStringValue) {
    return false;
  }

  return match(value.string_value());
}

bool StringMatcherImpl::match(const absl::string_view value) const {
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::StringMatcher::kExact:
    return matcher_.ignore_case() ? absl::EqualsIgnoreCase(value, matcher_.exact())
                                  : value == matcher_.exact();
  case envoy::type::matcher::StringMatcher::kPrefix:
    return matcher_.ignore_case() ? absl::StartsWithIgnoreCase(value, matcher_.prefix())
                                  : absl::StartsWith(value, matcher_.prefix());
  case envoy::type::matcher::StringMatcher::kSuffix:
    return matcher_.ignore_case() ? absl::EndsWithIgnoreCase(value, matcher_.suffix())
                                  : absl::EndsWith(value, matcher_.suffix());
  case envoy::type::matcher::StringMatcher::kRegex:
  case envoy::type::matcher::StringMatcher::kSafeRegex:
    return regex_->match(value);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool LowerCaseStringMatcher::match(const absl::string_view value) const {
  return matcher_.match(value);
}

bool LowerCaseStringMatcher::match(const ProtobufWkt::Value& value) const {
  return matcher_.match(value);
}

envoy::type::matcher::StringMatcher
LowerCaseStringMatcher::toLowerCase(const envoy::type::matcher::StringMatcher& matcher) {
  envoy::type::matcher::StringMatcher lowercase;
  switch (matcher.match_pattern_case()) {
  case envoy::type::matcher::StringMatcher::kRegex:
    lowercase.set_regex(StringUtil::toLower(matcher.regex()));
    break;
  case envoy::type::matcher::StringMatcher::kExact:
    lowercase.set_exact(StringUtil::toLower(matcher.exact()));
    break;
  case envoy::type::matcher::StringMatcher::kPrefix:
    lowercase.set_prefix(StringUtil::toLower(matcher.prefix()));
    break;
  case envoy::type::matcher::StringMatcher::kSuffix:
    lowercase.set_suffix(StringUtil::toLower(matcher.suffix()));
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
  return lowercase;
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

PathMatcherConstSharedPtr PathMatcher::createExact(const std::string& exact, bool ignore_case) {
  envoy::type::matcher::StringMatcher matcher;
  matcher.set_exact(exact);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher);
}

PathMatcherConstSharedPtr PathMatcher::createPrefix(const std::string& prefix, bool ignore_case) {
  envoy::type::matcher::StringMatcher matcher;
  matcher.set_prefix(prefix);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher);
}

bool MetadataMatcher::match(const envoy::api::v2::core::Metadata& metadata) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(metadata, matcher_.filter(), path_);
  return value_matcher_ && value_matcher_->match(value);
}

bool PathMatcher::match(const absl::string_view path) const {
  return matcher_.match(Http::PathUtil::removeQueryAndFragment(path));
}

} // namespace Matchers
} // namespace Envoy
