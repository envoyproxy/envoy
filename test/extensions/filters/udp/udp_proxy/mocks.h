#pragma once

#include "source/extensions/filters/udp/udp_proxy/session_filters/filter.h"

#include "test/mocks/stream_info/mocks.h"

#include "gmock/gmock.h"

using testing::NiceMock;

namespace Envoy {
namespace Extensions {
namespace UdpFilters {
namespace UdpProxy {
namespace SessionFilters {

class MockReadFilterCallbacks : public ReadFilterCallbacks {
public:
  MockReadFilterCallbacks();
  ~MockReadFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());
  MOCK_METHOD(void, continueFilterChain, ());

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

class MockWriteFilterCallbacks : public WriteFilterCallbacks {
public:
  MockWriteFilterCallbacks();
  ~MockWriteFilterCallbacks() override;

  MOCK_METHOD(uint64_t, sessionId, (), (const));
  MOCK_METHOD(StreamInfo::StreamInfo&, streamInfo, ());

  uint64_t session_id_{1};
  NiceMock<StreamInfo::MockStreamInfo> stream_info_;
};

} // namespace SessionFilters
} // namespace UdpProxy
} // namespace UdpFilters
} // namespace Extensions
} // namespace Envoy
