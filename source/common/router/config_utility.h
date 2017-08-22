#pragma once

#include <regex>
#include <string>
#include <vector>

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
   * See if the specified headers are present in the request headers.
   * @param headers supplies the list of headers to match
   * @param request_headers supplies the list of request headers to compare against search_list
   * @return true all the headers (and values) in the search_list set are found in the
   * request_headers
   */
  static bool matchHeaders(const Http::HeaderMap& headers,
                           const std::vector<HeaderData>& request_headers);
};

} // namespace Router
} // namespace Envoy
