#pragma once

#include <regex>
#include <vector>

#include "envoy/api/v2/route/route.pb.h"
#include "envoy/http/header_map.h"
#include "envoy/json/json_object.h"
#include "envoy/type/range.pb.h"

namespace Envoy {
namespace Http {

/**
 * Classes and methods for manipulating and checking HTTP headers.
 */
class HeaderUtility {
public:
  enum class HeaderMatchType { Value, Regex, Range, Present, Prefix, Suffix };

  // A HeaderData specifies one of exact value or regex or range element
  // to match in a request's header, specified in the header_match_type_ member.
  // It is the runtime equivalent of the HeaderMatchSpecifier proto in RDS API.
  struct HeaderData {
    HeaderData(const envoy::api::v2::route::HeaderMatcher& config);
    HeaderData(const Json::Object& config);

    const Http::LowerCaseString name_;
    HeaderMatchType header_match_type_;
    std::string value_;
    std::regex regex_pattern_;
    envoy::type::Int64Range range_;
    const bool invert_match_;
  };

  // A MatchOption specifies how to match the header value.
  struct MatchOption {
    // If true, remove the dot segments in the ":path" header before matching.
    // Single dot is removed directly from the path, double dots are removed
    // together with the preceding path segment (if exist). For example,
    // "/a/./c/../d" will be converted to "/a/d" before matching. See
    // https://tools.ietf.org/html/rfc3986#section-5.2.4.
    bool remove_dot_segments_in_path;
  };

  /**
   * See if the headers specified in the config are present in a request.
   * @param request_headers supplies the headers from the request.
   * @param config_headers supplies the list of configured header conditions on which to match.
   * @param MatchOption the match option that specifies how to match the header.
   * @return bool true if all the headers (and values) in the config_headers are found in the
   *         request_headers. If no config_headers are specified, returns true.
   */
  static bool matchHeaders(const Http::HeaderMap& request_headers,
                           const std::vector<HeaderData>& config_headers,
                           const MatchOption& match_option = MatchOption{});

  static bool matchHeaders(const Http::HeaderMap& request_headers, const HeaderData& config_header,
                           const MatchOption& match_option = MatchOption{});

  /**
   * Add headers from one HeaderMap to another
   * @param headers target where headers will be added
   * @param headers_to_add supplies the headers to be added
   */
  static void addHeaders(Http::HeaderMap& headers, const Http::HeaderMap& headers_to_add);
};
} // namespace Http
} // namespace Envoy
