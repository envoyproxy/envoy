#include "source/extensions/filters/udp/udp_proxy/session_filters/http_capsule/http_capsule.h"

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {
namespace HttpCapsule {

ReadFilterStatus HttpCapsuleFilter::onNewSession() {
  return ReadFilterStatus::Continue;
}

ReadFilterStatus HttpCapsuleFilter::onData(Network::UdpRecvData&) {
  return ReadFilterStatus::Continue;
}

WriteFilterStatus HttpCapsuleFilter::onWrite(Network::UdpRecvData&) {
  return WriteFilterStatus::Continue;
}

} // namespace HttpCapsule
} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
