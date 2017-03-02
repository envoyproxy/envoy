#pragma once

#include "envoy/json/json_object.h"
#include "envoy/upstream/resource_manager.h"

#include "common/http/headers.h"

namespace Router {

/**
 * Utility routines for loading route configuration and matching runtime request headers.
 */
class ConfigUtility {
public:
  struct HeaderData {
    HeaderData(const Http::LowerCaseString& name, const std::string& value, const bool is_regex)
        : name_(name), value_(value), regex_pattern_(value_, std::regex::optimize),
          is_regex_(is_regex) {}

    const Http::LowerCaseString name_;
    const std::string value_;
    const std::regex regex_pattern_;
    const bool is_regex_;
  };

  /**
   * @return the resource priority parsed from JSON.
   */
  static Upstream::ResourcePriority parsePriority(const Json::Object& config);

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

} // Router