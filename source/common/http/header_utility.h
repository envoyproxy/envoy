#pragma once

#include <vector>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/common/regex.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"
#include "envoy/type/range.pb.h"

#include "common/protobuf/protobuf.h"

namespace Envoy {
namespace Http {

/**
 * Classes and methods for manipulating and checking HTTP headers.
 */
class HeaderUtility {
public:
  enum class HeaderMatchType { Value, Regex, Range, Present, Prefix, Suffix };

  /**
   * Get all instances of the header key specified, and return the values in the vector provided.
   *
   * This should not be used for inline headers, as it turns a constant time lookup into O(n).
   *
   * @param headers the headers to return keys from
   * @param key the header key to return values for
   * @param out the vector to return values in
   */
  static void getAllOfHeader(const HeaderMap& headers, absl::string_view key,
                             std::vector<absl::string_view>& out);

  // A HeaderData specifies one of exact value or regex or range element
  // to match in a request's header, specified in the header_match_type_ member.
  // It is the runtime equivalent of the HeaderMatchSpecifier proto in RDS API.
  struct HeaderData : public HeaderMatcher {
    HeaderData(const envoy::api::v2::route::HeaderMatcher& config);

    const LowerCaseString name_;
    HeaderMatchType header_match_type_;
    std::string value_;
    Regex::CompiledMatcherPtr regex_;
    envoy::type::Int64Range range_;
    const bool invert_match_;

    // HeaderMatcher
    bool matchesHeaders(const HeaderMap& headers) const override {
      return HeaderUtility::matchHeaders(headers, *this);
    };
  };

  using HeaderDataPtr = std::unique_ptr<HeaderData>;

  /**
   * Build a vector of HeaderDataPtr given input config.
   */
  static std::vector<HeaderUtility::HeaderDataPtr> buildHeaderDataVector(
      const Protobuf::RepeatedPtrField<envoy::api::v2::route::HeaderMatcher>& header_matchers) {
    std::vector<HeaderUtility::HeaderDataPtr> ret;
    for (const auto& header_matcher : header_matchers) {
      ret.emplace_back(std::make_unique<HeaderUtility::HeaderData>(header_matcher));
    }
    return ret;
  }

  /**
   * Build a vector of HeaderMatcherSharedPtr given input config.
   */
  static std::vector<Http::HeaderMatcherSharedPtr> buildHeaderMatcherVector(
      const Protobuf::RepeatedPtrField<envoy::api::v2::route::HeaderMatcher>& header_matchers) {
    std::vector<Http::HeaderMatcherSharedPtr> ret;
    for (const auto& header_matcher : header_matchers) {
      ret.emplace_back(std::make_shared<HeaderUtility::HeaderData>(header_matcher));
    }
    return ret;
  }

  /**
   * See if the headers specified in the config are present in a request.
   * @param request_headers supplies the headers from the request.
   * @param config_headers supplies the list of configured header conditions on which to match.
   * @return bool true if all the headers (and values) in the config_headers are found in the
   *         request_headers. If no config_headers are specified, returns true.
   */
  static bool matchHeaders(const HeaderMap& request_headers,
                           const std::vector<HeaderDataPtr>& config_headers);

  static bool matchHeaders(const HeaderMap& request_headers, const HeaderData& config_header);

  /**
   * Validates that a header value is valid, according to RFC 7230, section 3.2.
   * http://tools.ietf.org/html/rfc7230#section-3.2
   * @return bool true if the header values are valid, according to the aforementioned RFC.
   */
  static bool headerIsValid(const absl::string_view header_value);

  /**
   * Add headers from one HeaderMap to another
   * @param headers target where headers will be added
   * @param headers_to_add supplies the headers to be added
   */
  static void addHeaders(HeaderMap& headers, const HeaderMap& headers_to_add);

  /**
   * @brief a helper function to determine if the headers represent an envoy internal request
   */
  static bool isEnvoyInternalRequest(const HeaderMap& headers);
};
} // namespace Http
} // namespace Envoy
