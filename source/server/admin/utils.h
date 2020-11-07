#pragma once

#include <regex>

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/init/manager.h"

#include "common/http/codes.h"
#include "common/http/header_map_impl.h"
#include "common/http/utility.h"

namespace Envoy {
namespace Server {
namespace Utility {

envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

void populateFallbackResponseHeaders(Http::Code code, Http::ResponseHeaderMap& header_map);

bool filterParam(Http::Utility::QueryParams params, Buffer::Instance& response,
                 absl::optional<std::regex>& regex);

absl::optional<std::string> formatParam(const Http::Utility::QueryParams& params);

absl::optional<std::string> queryParam(const Http::Utility::QueryParams& params,
                                       const std::string& key);

} // namespace Utility
} // namespace Server
} // namespace Envoy
