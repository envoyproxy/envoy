#include "source/common/network/application_protocol.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

auto application_protocols_inline_key =
    InlineMapRegistryHelper::registerInlinKey<StreamInfo::FilterStateInlineMapScope>(
        "envoy.network.application_protocols");

const StreamInfo::InlineKey ApplicationProtocols::key() { return application_protocols_inline_key; }

} // namespace Network
} // namespace Envoy
