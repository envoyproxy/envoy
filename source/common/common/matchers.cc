#include "common/common/matchers.h"

#include "envoy/api/v2/core/base.pb.h"

#include "common/config/metadata.h"

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

} // namespace Matchers
} // namespace Envoy
