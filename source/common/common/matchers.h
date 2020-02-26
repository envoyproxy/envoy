#pragma once

#include <string>

#include "envoy/common/matchers.h"
#include "envoy/common/regex.h"
#include "envoy/config/core/v3/base.pb.h"
#include "envoy/type/matcher/v3/metadata.pb.h"
#include "envoy/type/matcher/v3/number.pb.h"
#include "envoy/type/matcher/v3/path.pb.h"
#include "envoy/type/matcher/v3/string.pb.h"
#include "envoy/type/matcher/v3/value.pb.h"

#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Matchers {

class ValueMatcher;
using ValueMatcherConstSharedPtr = std::shared_ptr<const ValueMatcher>;

class PathMatcher;
using PathMatcherConstSharedPtr = std::shared_ptr<const PathMatcher>;

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
  static ValueMatcherConstSharedPtr create(const envoy::type::matcher::v3::ValueMatcher& value);
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
  DoubleMatcher(const envoy::type::matcher::v3::DoubleMatcher& matcher) : matcher_(matcher) {}

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::v3::DoubleMatcher matcher_;
};

class StringMatcherImpl : public ValueMatcher, public StringMatcher {
public:
  explicit StringMatcherImpl(const envoy::type::matcher::v3::StringMatcher& matcher);

  bool match(const absl::string_view value) const override;
  bool match(const ProtobufWkt::Value& value) const override;

  const envoy::type::matcher::v3::StringMatcher& matcher() const { return matcher_; }

private:
  const envoy::type::matcher::v3::StringMatcher matcher_;
  Regex::CompiledMatcherPtr regex_;
};

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::v3::ListMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::v3::ListMatcher matcher_;

  ValueMatcherConstSharedPtr oneof_value_matcher_;
};

class MetadataMatcher {
public:
  MetadataMatcher(const envoy::type::matcher::v3::MetadataMatcher& matcher);

  /**
   * Check whether the metadata is matched to the matcher.
   * @param metadata the metadata to check.
   * @return true if it's matched otherwise false.
   */
  bool match(const envoy::config::core::v3::Metadata& metadata) const;

private:
  const envoy::type::matcher::v3::MetadataMatcher matcher_;
  std::vector<std::string> path_;

  ValueMatcherConstSharedPtr value_matcher_;
};

class PathMatcher : public StringMatcher {
public:
  PathMatcher(const envoy::type::matcher::v3::PathMatcher& path) : matcher_(path.path()) {}
  PathMatcher(const envoy::type::matcher::v3::StringMatcher& matcher) : matcher_(matcher) {}

  static PathMatcherConstSharedPtr createExact(const std::string& exact, bool ignore_case);
  static PathMatcherConstSharedPtr createPrefix(const std::string& prefix, bool ignore_case);

  bool match(const absl::string_view path) const override;

private:
  const StringMatcherImpl matcher_;
};

} // namespace Matchers
} // namespace Envoy
