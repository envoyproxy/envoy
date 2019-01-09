#include "extensions/quic_listeners/quiche/dummy.h"

using http2::Http2String;

namespace Envoy {
namespace Extensions {
namespace QuicListeners {
namespace Quiche {

Http2String moreCowbell(const Http2String& s) {
  return s + " cowbell";
}

} // namespace Quiche
} // namespace QuicListeners
} // namespace Extensions
} // namespace Envoy
