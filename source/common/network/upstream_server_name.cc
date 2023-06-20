#include "source/common/network/upstream_server_name.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

auto upstream_server_name_inline_key =
    InlineMapRegistryHelper::registerInlinKey<StreamInfo::FilterStateInlineMapScope>(
        "envoy.network.upstream_server_name");

const StreamInfo::InlineKey UpstreamServerName::key() { return upstream_server_name_inline_key; }

} // namespace Network
} // namespace Envoy
