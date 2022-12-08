#pragma once

#include "envoy/admin/v3/server_info.pb.h"
#include "envoy/init/manager.h"

namespace Envoy {
namespace Server {
namespace Utility {

envoy::admin::v3::ServerInfo::State serverState(Init::Manager::State state,
                                                bool health_check_failed);

} // namespace Utility
} // namespace Server
} // namespace Envoy
