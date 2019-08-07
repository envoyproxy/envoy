#pragma once

#include <string>

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

// Placeholder use of a QUICHE platform type.
// TODO(mpwarres): remove once real uses of QUICHE platform added.
std::string moreCowbell(const std::string& s);

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
