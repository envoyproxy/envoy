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
    return false;
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
    return false;
  }
}

MetadataMatcher::MetadataMatcher(const envoy::type::matcher::MetadataMatcher& matcher)
    : matcher_(matcher), path_(matcher.path().begin(), matcher.path().end()) {
  for (const envoy::type::matcher::MetadataMatcher::Value& m : matcher_.values()) {
    switch (m.match_pattern_case()) {
    case envoy::type::matcher::MetadataMatcher_Value::kNullMatch:
      null_matcher_ |= m.null_match();
      break;
    case envoy::type::matcher::MetadataMatcher_Value::kDoubleMatch:
      double_matcher_.push_back(DoubleMatcher(m.double_match()));
      break;
    case envoy::type::matcher::MetadataMatcher_Value::kStringMatch:
      string_matcher_.push_back(StringMatcher(m.string_match()));
      break;
    case envoy::type::matcher::MetadataMatcher_Value::kBoolMatch:
      if (m.bool_match()) {
        bool_matcher_allow_true_ = true;
      } else {
        bool_matcher_allow_false_ = true;
      }
      break;
    case envoy::type::matcher::MetadataMatcher_Value::kPresentMatch:
      present_matcher_ |= m.present_match();
      break;
    default:
      break;
    }
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
    for (const auto& m : double_matcher_) {
      if (m.match(value.number_value())) {
        return true;
      }
    }
    return false;
  case ProtobufWkt::Value::kStringValue:
    for (const auto& m : string_matcher_) {
      if (m.match(value.string_value())) {
        return true;
      }
    }
    return false;
  case ProtobufWkt::Value::kBoolValue:
    return (bool_matcher_allow_true_ && value.bool_value()) ||
           (bool_matcher_allow_false_ && !value.bool_value());
  default:
    return false;
  }
}

} // namespace Matchers
} // namespace Envoy
