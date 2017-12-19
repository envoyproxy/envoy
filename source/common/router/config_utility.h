#pragma once

#include <regex>
#include <string>
#include <vector>

#include "envoy/http/codes.h"
#include "envoy/json/json_object.h"
#include "envoy/upstream/resource_manager.h"

#include "common/common/empty_string.h"
#include "common/config/rds_json.h"
#include "common/http/headers.h"
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
          regex_pattern_(value_, std::regex::optimize),
          is_regex_(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, regex, false)) {}
    HeaderData(const Json::Object& config)
        : HeaderData([&config] {
            envoy::api::v2::HeaderMatcher header_matcher;
            Envoy::Config::RdsJson::translateHeaderMatcher(config, header_matcher);
            return header_matcher;
          }()) {}

    const Http::LowerCaseString name_;
    const std::string value_;
    const std::regex regex_pattern_;
    const bool is_regex_;
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
   * Returns the redirect HTTP Status Code enum parsed from proto.
   * @param code supplies the RedirectResponseCode enum.
   * @return Returns the Http::Code version of the RedirectResponseCode.
   */
  static Http::Code
  parseRedirectResponseCode(const envoy::api::v2::RedirectAction::RedirectResponseCode& code);

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
