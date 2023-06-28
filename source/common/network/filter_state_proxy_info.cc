#include "source/common/network/filter_state_proxy_info.h"

namespace Envoy {
namespace Network {

REGISTER_INLINE_KEY(StreamInfo::FilterStateInlineMapScope, http11_proxy_info_inline_key,
                    "envoy.network.transport_socket.http_11_proxy.info");

const StreamInfo::InlineKey Http11ProxyInfoFilterState::key() {
  return http11_proxy_info_inline_key;
}

} // namespace Network
} // namespace Envoy
