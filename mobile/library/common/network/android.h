#pragma once

namespace Envoy {
namespace Network {
namespace Android {
namespace Utility {
/**
 * Sets an alternate `getifaddrs` implementation than the one defined
 * in Envoy by default.
 */
void setAlternateGetifaddrs();
} // namespace Utility
} // namespace Android
} // namespace Network
} // namespace Envoy
