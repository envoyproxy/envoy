#pragma once

#include <string>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/common/matchers.h"
#include "envoy/common/regex.h"
#include "envoy/type/matcher/metadata.pb.h"
#include "envoy/type/matcher/number.pb.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/type/matcher/value.pb.h"

#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Matchers {

class ValueMatcher;
using ValueMatcherConstSharedPtr = std::shared_ptr<const ValueMatcher>;

class ValueMatcher {
public:
  virtual ~ValueMatcher() = default;

  /**
   * Check whether the value is matched to the matcher.
   */
  virtual bool match(const ProtobufWkt::Value& value) const PURE;

  /**
   * Create the matcher object.
   */
  static ValueMatcherConstSharedPtr create(const envoy::type::matcher::ValueMatcher& value);
};

class NullMatcher : public ValueMatcher {
public:
  /**
   * Check whether the value is NULL.
   */
  bool match(const ProtobufWkt::Value& value) const override;
};

class BoolMatcher : public ValueMatcher {
public:
  BoolMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const bool matcher_;
};

class PresentMatcher : public ValueMatcher {
public:
  PresentMatcher(bool matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const bool matcher_;
};

class DoubleMatcher : public ValueMatcher {
public:
  DoubleMatcher(const envoy::type::matcher::DoubleMatcher& matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::DoubleMatcher matcher_;
};

class StringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  explicit StringMatcherImpl(const envoy::type::matcher::StringMatcher& matcher);

  bool match(const absl::string_view value) const override;
  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::StringMatcher matcher_;
  Regex::CompiledMatcherPtr regex_;
};

class LowerCaseStringMatcher : public ValueMatcher {
public:
  LowerCaseStringMatcher(const envoy::type::matcher::StringMatcher& matcher)
      : matcher_(toLowerCase(matcher)) {}

  bool match(const absl::string_view value) const;

  bool match(const ProtobufWkt::Value& value) const override;

private:
  envoy::type::matcher::StringMatcher
  toLowerCase(const envoy::type::matcher::StringMatcher& matcher);

  const StringMatcherImpl matcher_;
};

using LowerCaseStringMatcherPtr = std::unique_ptr<LowerCaseStringMatcher>;

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::ListMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::ListMatcher matcher_;

  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matcher::MetadataMatcher& matcher);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const envoy::api::v2::core::Metadata& metadata) const;

private:
  const envoy::type::matcher::MetadataMatcher matcher_;
  std::vector<std::string> path_;

  ValueMatcherConstSharedPtr value_matcher_;
};

} // namespace Matchers
} // namespace Envoy
