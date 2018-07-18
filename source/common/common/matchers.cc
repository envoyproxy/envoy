#include "common/common/matchers.h"

#include "envoy/api/v2/core/base.pb.h"

#include "common/config/metadata.h"
#include "common/protobuf/protobuf.h"

#include "absl/strings/match.h"

namespace Envoy {
namespace Matchers {

bool DoubleMatcher::match(double value) const {
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::DoubleMatcher::kRange:
    return matcher_.range().start() <= value && value < matcher_.range().end();
  case envoy::type::matcher::DoubleMatcher::kExact:
    return matcher_.exact() == value;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  };
}

bool StringMatcher::match(const std::string& value) const {
  switch (matcher_.match_pattern_case()) {
  case envoy::type::matcher::StringMatcher::kExact:
    return matcher_.exact() == value;
  case envoy::type::matcher::StringMatcher::kPrefix:
    return absl::StartsWith(value, matcher_.prefix());
  case envoy::type::matcher::StringMatcher::kSuffix:
    return absl::EndsWith(value, matcher_.suffix());
  case envoy::type::matcher::StringMatcher::kRegex:
    return std::regex_match(value, regex_);
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

MetadataMatcher::MetadataMatcher(const envoy::type::matcher::MetadataMatcher& matcher)
    : matcher_(matcher) {
  for (const auto& seg : matcher.path()) {
    path_.push_back(seg.key());
  }
  const auto& v = matcher_.value();
  switch (v.match_pattern_case()) {
  case envoy::type::matcher::MetadataMatcher_Value::kNullMatch:
    null_matcher_ = true;
    break;
  case envoy::type::matcher::MetadataMatcher_Value::kDoubleMatch:
    double_matcher_.emplace(v.double_match());
    break;
  case envoy::type::matcher::MetadataMatcher_Value::kStringMatch:
    string_matcher_.emplace(v.string_match());
    break;
  case envoy::type::matcher::MetadataMatcher_Value::kBoolMatch:
    bool_matcher_.emplace(v.bool_match());
    break;
  case envoy::type::matcher::MetadataMatcher_Value::kPresentMatch:
    present_matcher_ = v.present_match();
    break;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

bool MetadataMatcher::match(const envoy::api::v2::core::Metadata& metadata) const {
  const auto& value = Envoy::Config::Metadata::metadataValue(metadata, matcher_.filter(), path_);
  if (present_matcher_ && value.kind_case() != ProtobufWkt::Value::KIND_NOT_SET) {
    return true;
  }
  switch (value.kind_case()) {
  case ProtobufWkt::Value::kNullValue:
    return null_matcher_;
  case ProtobufWkt::Value::kNumberValue:
    return double_matcher_.has_value() && double_matcher_->match(value.number_value());
  case ProtobufWkt::Value::kStringValue:
    return string_matcher_.has_value() && string_matcher_->match(value.string_value());
  case ProtobufWkt::Value::kBoolValue:
    return (bool_matcher_.has_value() && *bool_matcher_ == value.bool_value());
  case ProtobufWkt::Value::kListValue:
  case ProtobufWkt::Value::kStructValue:
  case ProtobufWkt::Value::KIND_NOT_SET:
    return false;
  default:
    NOT_REACHED_GCOVR_EXCL_LINE;
  }
}

} // namespace Matchers
} // namespace Envoy
