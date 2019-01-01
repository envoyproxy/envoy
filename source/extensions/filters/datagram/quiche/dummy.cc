#include "extensions/filters/datagram/quiche/dummy.h"

using http2::Http2String;

namespace Envoy {
namespace Extensions {
namespace DatagramFilters {
namespace Quiche {

Http2String moreCowbell(const Http2String& s) {
  return s + " cowbell";
}

} // namespace Quiche
} // namespace DatagramFilters
} // namespace Extensions
} // namespace Envoy
