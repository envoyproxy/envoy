#include "source/common/network/filter_state_proxy_info.h"

namespace Envoy {
namespace Network {

auto http11_proxy_info_inline_key =
    InlineMapRegistryHelper::registerInlinKey<StreamInfo::FilterStateInlineMapScope>(
        "envoy.network.transport_socket.http_11_proxy.info");

const StreamInfo::InlineKey Http11ProxyInfoFilterState::key() {
  return http11_proxy_info_inline_key;
}

} // namespace Network
} // namespace Envoy
