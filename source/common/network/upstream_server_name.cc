#include "source/common/network/upstream_server_name.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

constexpr absl::string_view UpstreamServerNameKey = "envoy.network.upstream_server_name";

REGISTER_INLINE_MAP_KEY(StreamInfo::FilterStateInlineMapScope, UpstreamServerNameKey);

const StreamInfo::InlineKey UpstreamServerName::key() {
  INLINE_HANDLE_BY_KEY_ON_FIRST_USE(StreamInfo::FilterStateInlineMapScope, UpstreamServerNameKey);
}

} // namespace Network
} // namespace Envoy
