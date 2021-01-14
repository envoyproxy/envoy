#include "server/admin/utils.h"

#include "common/common/enum_to_int.h"
#include "common/http/headers.h"

namespace Envoy {
namespace Server {
namespace Utility {

envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed) {
  switch (state) {
  case Init::Manager::State::Uninitialized:
    return envoy::admin::v3::ServerInfo::PRE_INITIALIZING;
  case Init::Manager::State::Initializing:
    return envoy::admin::v3::ServerInfo::INITIALIZING;
  case Init::Manager::State::Initialized:
    return health_check_failed ? envoy::admin::v3::ServerInfo::DRAINING
                               : envoy::admin::v3::ServerInfo::LIVE;
  }
  NOT_REACHED_GCOVR_EXCL_LINE;
}

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map) {
  header_map.setStatus(std::to_string(enumToInt(code)));
  if (header_map.ContentType() == nullptr) {
    // Default to text-plain if unset.
    header_map.setReferenceContentType(Http::Headers::get().ContentTypeValues.TextUtf8);
  }
  // Default to 'no-cache' if unset, but not 'no-store' which may break the back button.
  if (header_map.get(Http::CustomHeaders::get().CacheControl).empty()) {
    header_map.setReference(Http::CustomHeaders::get().CacheControl,
                            Http::CustomHeaders::get().CacheControlValues.NoCacheMaxAge0);
  }

  // Under no circumstance should browsers sniff content-type.
  header_map.addReference(Http::Headers::get().XContentTypeOptions,
                          Http::Headers::get().XContentTypeOptionValues.Nosniff);
}

// Helper method to get filter parameter, or report an error for an invalid regex.
bool filterParam(Http::Utility::QueryParams params, Buffer::Instance& response,
                 absl::optional<std::regex>& regex) {
  auto p = params.find("filter");
  if (p != params.end()) {
    const std::string& pattern = p->second;
    try {
      regex = std::regex(pattern);
    } catch (std::regex_error& error) {
      // Include the offending pattern in the log, but not the error message.
      response.add(fmt::format("Invalid regex: \"{}\"\n", error.what()));
      ENVOY_LOG_MISC(error, "admin: Invalid regex: \"{}\": {}", error.what(), pattern);
      return false;
    }
  }
  return true;
}

// Helper method to get the format parameter.
absl::optional<std::string> formatParam(const Http::Utility::QueryParams& params) {
  return queryParam(params, "format");
}

// Helper method to get a query parameter.
absl::optional<std::string> queryParam(const Http::Utility::QueryParams& params,
                                       const std::string& key) {
  return (params.find(key) != params.end()) ? absl::optional<std::string>{params.at(key)}
                                            : absl::nullopt;
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
