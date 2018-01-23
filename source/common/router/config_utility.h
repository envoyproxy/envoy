#pragma once

#include <regex>
#include <string>
#include <vector>

#include "envoy/common/optional.h"
#include "envoy/http/codes.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/empty_string.h"
#include "common/common/utility.h"
#include "common/config/rds_json.h"
#include "common/http/headers.h"
#include "common/http/utility.h"
#include "common/protobuf/utility.h"

#include "api/rds.pb.h"

namespace Envoy {
namespace Router {

/**
 * Utility routines for loading route configuration and matching runtime request headers.
 */
class ConfigUtility {
public:
  struct HeaderData {
    // An empty header value allows for matching to be only based on header presence.
    // Regex is an opt-in. Unless explicitly mentioned, the header values will be used for
    // exact string matching.
    HeaderData(const envoy::api::v2::HeaderMatcher& config)
        : name_(config.name()), value_(config.value()),
          is_regex_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)),
          regex_pattern_(is_regex_ ? RegexUtil::parseRegex(value_) : std::regex()) {}
    HeaderData(const Json::Object& config)
        : HeaderData([&config] {
            envoy::api::v2::HeaderMatcher header_matcher;
            Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
            return header_matcher;
          }()) {}

    const Http::LowerCaseString name_;
    const std::string value_;
    const bool is_regex_;
    const std::regex regex_pattern_;
  };

  // A QueryParameterMatcher specifies one "name" or "name=value" element
  // to match in a request's query string. It is the optimized, runtime
  // equivalent of the QueryParameterMatcher proto in the RDS v2 API.
  class QueryParameterMatcher {
  public:
    QueryParameterMatcher(const envoy::api::v2::QueryParameterMatcher& config)
        : name_(config.name()), value_(config.value()),
          is_regex_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)),
          regex_pattern_(is_regex_ ? RegexUtil::parseRegex(value_) : std::regex()) {}

    /**
     * Check if the query parameters for a request contain a match for this
     * QueryParameterMatcher.
     * @param request_query_params supplies the parsed query parameters from a request.
     * @return bool true if a match for this QueryParameterMatcher exists in request_query_params.
     */
    bool matches(const Http::Utility::QueryParams& request_query_params) const;

  private:
    const std::string name_;
    const std::string value_;
    const bool is_regex_;
    const std::regex regex_pattern_;
  };

  /**
   * @return the resource priority parsed from proto.
   */
  static Upstream::ResourcePriority parsePriority(const envoy::api::v2::RoutingPriority& priority);

  /**
   * See if the headers specified in the config are present in a request.
   * @param request_headers supplies the headers from the request.
   * @param config_headers supplies the list of configured header conditions on which to match.
   * @return bool true if all the headers (and values) in the config_headers are found in the
   *         request_headers
   */
  static bool matchHeaders(const Http::HeaderMap& request_headers,
                           const std::vector<HeaderData>& config_headers);

  /**
   * See if the query parameters specified in the config are present in a request.
   * @param query_params supplies the query parameters from the request's query string.
   * @param config_params supplies the list of configured query param conditions on which to match.
   * @return bool true if all the query params (and values) in the config_params are found in the
   *         query_params
   */
  static bool matchQueryParams(const Http::Utility::QueryParams& query_params,
                               const std::vector<QueryParameterMatcher>& config_query_params);

  /**
   * Returns the redirect HTTP Status Code enum parsed from proto.
   * @param code supplies the RedirectResponseCode enum.
   * @return Returns the Http::Code version of the RedirectResponseCode.
   */
  static Http::Code
  parseRedirectResponseCode(const envoy::api::v2::RedirectAction::RedirectResponseCode& code);

  /**
   * Returns the HTTP Status Code enum parsed from the route's redirect or direct_response.
   * @param route supplies the Route configuration.
   * @return Optional<Http::Code> the HTTP status from the route's direct_response if specified,
   *         or the HTTP status code from the route's redirect if specified,
   *         or an empty Option otherwise.
   */
  static Optional<Http::Code> parseDirectResponseCode(const envoy::api::v2::Route& route);

  /**
   * Returns the content of the response body to send with direct responses from a route.
   * @param route supplies the Route configuration.
   * @return Optional<std::string> the response body provided inline in the route's
   *         direct_response if specified, or the contents of the file named in the
   *         route's direct_response if specified, or an empty string otherwise.
   * @throw EnvoyException if the route configuration contains an error.
   */
  static std::string parseDirectResponseBody(const envoy::api::v2::Route& route);

  /**
   * Returns the HTTP Status Code enum parsed from proto.
   * @param code supplies the ClusterNotFoundResponseCode enum.
   * @return Returns the Http::Code version of the ClusterNotFoundResponseCode enum.
   */
  static Http::Code parseClusterNotFoundResponseCode(
      const envoy::api::v2::RouteAction::ClusterNotFoundResponseCode& code);
};

} // namespace Router
} // namespace Envoy
