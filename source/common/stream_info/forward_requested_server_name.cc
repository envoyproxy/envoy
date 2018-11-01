#include "common/stream_info/forward_requested_server_name.h"

namespace Envoy {
namespace StreamInfo {

const std::string ForwardRequestedServerName::Key =
    "envoy.stream_info.original_requested_server_name";

} // namespace StreamInfo
} // namespace Envoy
