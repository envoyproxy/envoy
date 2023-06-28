#include "source/common/network/upstream_server_name.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

REGISTER_INLINE_KEY(StreamInfo::FilterStateInlineMapScope, upstream_server_name_inline_key,
                    "envoy.network.upstream_server_name");

const StreamInfo::InlineKey UpstreamServerName::key() { return upstream_server_name_inline_key; }

} // namespace Network
} // namespace Envoy
