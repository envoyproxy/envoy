#pragma once

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/init/manager.h"

#include "common/http/codes.h"
#include "common/http/header_map_impl.h"

namespace Envoy {
namespace Server {
namespace Utility {

envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map);

} // namespace Utility
} // namespace Server
} // namespace Envoy
