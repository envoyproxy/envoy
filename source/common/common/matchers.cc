#include "source/common/common/matchers.h"

#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"
#include "envoy/type/matcher/v3/number.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/matcher/v3/value.pb.h"

#include "source/common/common/macros.h"
#include "source/common/common/regex.h"
#include "source/common/config/metadata.h"
#include "source/common/http/path_utility.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Matchers {

ValueMatcherConstSharedPtr ValueMatcher::create(const envoy::type::matcher::v3::ValueMatcher& v) {
  switch (v.match_pattern_case()) {
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kNullMatch:
    return std::make_shared<const NullMatcher>();
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kDoubleMatch:
    return std::make_shared<const DoubleMatcher>(v.double_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kStringMatch:
    return std::make_shared<const StringMatcherImpl<std::decay_t<decltype(v.string_match())>>>(
        v.string_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kBoolMatch:
    return std::make_shared<const BoolMatcher>(v.bool_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kPresentMatch:
    return std::make_shared<const PresentMatcher>(v.present_match());
  case envoy::type::matcher::v3::ValueMatcher::MatchPatternCase::kListMatch:
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
  case envoy::type::matcher::v3::DoubleMatcher::MatchPatternCase::kRange:
    return matcher_.range().start() <= v && v < matcher_.range().end();
  case envoy::type::matcher::v3::DoubleMatcher::MatchPatternCase::kExact:
    return matcher_.exact() == v;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  };
}

ListMatcher::ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher) : matcher_(matcher) {
  ASSERT(matcher_.match_pattern_case() ==
         envoy::type::matcher::v3::ListMatcher::MatchPatternCase::kOneOf);

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

MetadataMatcher::MetadataMatcher(const envoy::type::matcher::v3::MetadataMatcher& matcher)
    : matcher_(matcher) {
  for (const auto& seg : matcher.path()) {
    path_.push_back(seg.key());
  }
  const auto& v = matcher_.value();
  value_matcher_ = ValueMatcher::create(v);
}

PathMatcherConstSharedPtr PathMatcher::createExact(const std::string& exact, bool ignore_case) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_exact(exact);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher);
}

PathMatcherConstSharedPtr PathMatcher::createPrefix(const std::string& prefix, bool ignore_case) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.set_prefix(prefix);
  matcher.set_ignore_case(ignore_case);
  return std::make_shared<const PathMatcher>(matcher);
}

PathMatcherConstSharedPtr
PathMatcher::createSafeRegex(const envoy::type::matcher::v3::RegexMatcher& regex_matcher) {
  envoy::type::matcher::v3::StringMatcher matcher;
  matcher.mutable_safe_regex()->MergeFrom(regex_matcher);
  return std::make_shared<const PathMatcher>(matcher);
}

bool MetadataMatcher::match(const envoy::config::core::v3::Metadata& metadata) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(&metadata, matcher_.filter(), path_);
  return value_matcher_ && value_matcher_->match(value);
}

bool PathMatcher::match(const absl::string_view path) const {
  return matcher_.match(Http::PathUtil::removeQueryAndFragment(path));
}

} // namespace Matchers
} // namespace Envoy
