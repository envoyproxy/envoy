#pragma once

#include "envoy/network/address.h"
#include "envoy/network/listen_socket.h"

namespace Envoy {
namespace Extensions {
namespace Filters {
namespace Common {
namespace OriginalSrc {

Network::Socket::OptionsSharedPtr
buildOriginalSrcOptions(Network::Address::InstanceConstSharedPtr source, uint32_t mark);

} // namespace OriginalSrc
} // namespace Common
} // namespace Filters
} // namespace Extensions
} // namespace Envoy
