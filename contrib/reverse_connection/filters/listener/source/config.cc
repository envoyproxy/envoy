#include "contrib/reverse_connection//filters/listener/source/config.h"

#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace ListenerFilters {
namespace ReverseConnection {

Config::Config(
    const envoy::extensions::filters::listener::reverse_connection::v3alpha::ReverseConnection& config)
    : ping_wait_timeout_(
          std::chrono::seconds(PROTOBUF_GET_WRAPPED_OR_DEFAULT(config, ping_wait_timeout, 10))) {}

} // namespace ReverseConnection
} // namespace ListenerFilters
} // namespace Extensions
} // namespace Envoy
