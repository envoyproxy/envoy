#include "extensions/quic_listeners/quiche/dummy.h"

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

// Placeholder use of a QUICHE platform type.
// TODO(mpwarres): remove once real uses of QUICHE platform added.
std::string moreCowbell(const std::string& s) { return s + " cowbell"; }

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
