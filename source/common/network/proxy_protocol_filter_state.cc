#include "source/common/network/proxy_protocol_filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

REGISTER_INLINE_KEY(StreamInfo::FilterStateInlineMapScope, proxy_protocol_options_inline_key,
                    "envoy.network.proxy_protocol_options");

const StreamInfo::InlineKey ProxyProtocolFilterState::key() {
  return proxy_protocol_options_inline_key;
}

} // namespace Network
} // namespace Envoy
