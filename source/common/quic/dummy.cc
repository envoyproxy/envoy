#include "common/quic/dummy.h"

using http2::Http2String;

namespace Envoy {
namespace Quic {

Http2String moreCowbell(const Http2String& s) {
  return s + " cowbell";
}

}  // namespace Quic
}  // namespace Envoy
