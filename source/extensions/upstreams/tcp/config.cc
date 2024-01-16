#include "source/extensions/upstreams/tcp/config.h"

#include <chrono>
#include <memory>

#include "envoy/upstream/upstream.h"

#include "source/common/config/utility.h"
#include "source/common/protobuf/utility.h"

namespace Envoy {
namespace Extensions {
namespace Upstreams {
namespace Tcp {

ProtocolOptionsConfigImpl::ProtocolOptionsConfigImpl(
    const envoy::extensions::upstreams::tcp::v3::TcpProtocolOptions& options) {
  if (options.has_idle_timeout()) {
    idle_timeout_ =
        std::chrono::milliseconds(DurationUtil::durationToMilliseconds(options.idle_timeout()));
  }
}

LEGACY_REGISTER_FACTORY(ProtocolOptionsConfigFactory, Server::Configuration::ProtocolOptionsFactory,
                        "envoy.upstreams.tcp.tcp_protocol_options");
} // namespace Tcp
} // namespace Upstreams
} // namespace Extensions
} // namespace Envoy
