#pragma once

#include "absl/strings/string_view.h"

namespace Envoy {
namespace Extensions {
namespace IoSocket {
namespace UserSpace {

// Key under which the originating connection ID is stored in the upstream
// filter state.
constexpr absl::string_view ConnectionIdFilterStateKey =
    "envoy.io_socket.user_space.connection_id";

} // namespace UserSpace
} // namespace IoSocket
} // namespace Extensions
} // namespace Envoy
