#include "test/extensions/filters/udp/udp_proxy/mocks.h"

#include "gtest/gtest.h"

using testing::Return;
using testing::ReturnRef;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

MockReadFilterCallbacks::MockReadFilterCallbacks() {
  ON_CALL(*this, sessionId()).WillByDefault(Return(session_id_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockReadFilterCallbacks::~MockReadFilterCallbacks() = default;

MockWriteFilterCallbacks::MockWriteFilterCallbacks() {
  ON_CALL(*this, sessionId()).WillByDefault(Return(session_id_));
  ON_CALL(*this, streamInfo()).WillByDefault(ReturnRef(stream_info_));
}
MockWriteFilterCallbacks::~MockWriteFilterCallbacks() = default;

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
