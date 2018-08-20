#pragma once

#include <regex>
#include <string>
#include <vector>

#include "envoy/api/v2/core/base.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"
#include "envoy/type/matcher/header.pb.h"
#include "envoy/type/matcher/metadata.pb.h"
#include "envoy/type/matcher/number.pb.h"
#include "envoy/type/matcher/string.pb.h"
#include "envoy/type/matcher/value.pb.h"
#include "envoy/type/range.pb.h"

#include "common/common/utility.h"
#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Matchers {

class ValueMatcher;
typedef std::shared_ptr<const ValueMatcher> ValueMatcherConstSharedPtr;

class ValueMatcher {
public:
  virtual ~ValueMatcher() {}

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

class StringMatcher : public ValueMatcher {
public:
  StringMatcher(const envoy::type::matcher::StringMatcher& matcher) : matcher_(matcher) {
    if (matcher.match_pattern_case() == envoy::type::matcher::StringMatcher::kRegex) {
      regex_ = RegexUtil::parseRegex(matcher_.regex());
    }
  }

  bool match(const ProtobufWkt::Value& value) const override;

private:
  const envoy::type::matcher::StringMatcher matcher_;
  std::regex regex_;
};

class ListMatcher : public ValueMatcher {
public:
  ListMatcher(const envoy::type::matcher::ListMatcher& matcher);

  bool match(const ProtobufWkt::Value& value) const;

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

/**
 * Classes and methods for manipulating and chcecking HTTP headers.
 */
class HeaderUtility {
public:
  enum class HeaderMatchType { Value, Regex, Range, Present, Prefix, Suffix };

  // A HeaderData specifies one of exact value or regex or range element
  // to match in a request's header, specified in the header_match_type_ member.
  // It is the runtime equivalent of the HeaderMatchSpecifier proto in RDS API.
  struct HeaderData {
    HeaderData(const envoy::type::matcher::HeaderMatcher& config);
    HeaderData(const Json::Object& config);

    const Http::LowerCaseString name_;
    HeaderMatchType header_match_type_;
    std::string value_;
    std::regex regex_pattern_;
    envoy::type::Int64Range range_;
    const bool invert_match_;
  };

  /**
   * See if the headers specified in the config are present in a request.
   * @param request_headers supplies the headers from the request.
   * @param config_headers supplies the list of configured header conditions on which to match.
   * @return bool true if all the headers (and values) in the config_headers are found in the
   *         request_headers
   */
  static bool matchHeaders(const Http::HeaderMap& request_headers,
                           const std::vector<HeaderData>& config_headers);

  static bool matchHeaders(const Http::HeaderMap& request_headers, const HeaderData& config_header);

  /**
   * Add headers from one HeaderMap to another
   * @param headers target where headers will be added
   * @param headers_to_add supplies the headers to be added
   */
  static void addHeaders(Http::HeaderMap& headers, const Http::HeaderMap& headers_to_add);
};

} // namespace Matchers
} // namespace Envoy
