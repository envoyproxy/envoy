#include "source/common/network/application_protocol.h"

#include "source/common/common/macros.h"

namespace Envoy {
namespace Network {

constexpr absl::string_view ApplicationProtocolsKey = "envoy.network.application_protocols";

REGISTER_INLINE_MAP_KEY(StreamInfo::FilterStateInlineMapScope, ApplicationProtocolsKey);

using Scope = StreamInfo::FilterStateInlineMapScope;

const StreamInfo::InlineKey ApplicationProtocols::key() {
  INLINE_HANDLE_BY_KEY_ON_FIRST_USE(StreamInfo::FilterStateInlineMapScope, ApplicationProtocolsKey);
}

} // namespace Network
} // namespace Envoy
