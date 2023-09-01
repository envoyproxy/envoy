#include "source/common/network/proxy_protocol_filter_state.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

constexpr absl::string_view ProxyProtocolFilterStateKey = "envoy.network.proxy_protocol_options";

REGISTER_INLINE_MAP_KEY(StreamInfo::FilterStateInlineMapScope, ProxyProtocolFilterStateKey);

const StreamInfo::InlineKey ProxyProtocolFilterState::key() {
  INLINE_HANDLE_BY_KEY_ON_FIRST_USE(StreamInfo::FilterStateInlineMapScope,
                                    ProxyProtocolFilterStateKey);
}

} // namespace Network
} // namespace Envoy
