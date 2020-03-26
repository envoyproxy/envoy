#include "server/http/utils.h"

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
  const auto& headers = Http::Headers::get();
  if (header_map.ContentType() == nullptr) {
    // Default to text-plain if unset.
    header_map.setReferenceContentType(headers.ContentTypeValues.TextUtf8);
  }
  // Default to 'no-cache' if unset, but not 'no-store' which may break the back button.
  if (header_map.CacheControl() == nullptr) {
    header_map.setReferenceCacheControl(headers.CacheControlValues.NoCacheMaxAge0);
  }

  // Under no circumstance should browsers sniff content-type.
  header_map.addReference(headers.XContentTypeOptions, headers.XContentTypeOptionValues.Nosniff);
}

} // namespace Utility
} // namespace Server
} // namespace Envoy
