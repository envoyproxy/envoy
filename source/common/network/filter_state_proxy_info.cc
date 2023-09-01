#include "source/common/network/filter_state_proxy_info.h"

namespace Envoy {
namespace Network {

constexpr absl::string_view Http11ProxyInfoFilterStateKey =
    "envoy.network.http_connection_manager.http1_proxy";

REGISTER_INLINE_MAP_KEY(StreamInfo::FilterStateInlineMapScope, Http11ProxyInfoFilterStateKey);

const StreamInfo::InlineKey Http11ProxyInfoFilterState::key() {
  INLINE_HANDLE_BY_KEY_ON_FIRST_USE(StreamInfo::FilterStateInlineMapScope,
                                    Http11ProxyInfoFilterStateKey);
}

} // namespace Network
} // namespace Envoy
