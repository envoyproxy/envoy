#include <chrono>

#include "common/http/date_provider_impl.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;
using testing::Return;

namespace Envoy {
namespace Http {

class MockTimeSource : public TimeSource {
public:
  MOCK_METHOD(SystemTime, systemTime, ());
  MOCK_METHOD(MonotonicTime, monotonicTime, ());
};

TEST(SlowDateProviderImplTest, RequestHeaders) {
  MockTimeSource time_source;
  SystemTime time_point{SystemTime::duration(2000000)};
  EXPECT_CALL(time_source, systemTime()).WillOnce(Return(time_point));

  SlowDateProviderImpl provider(time_source);
  TestRequestHeaderMapImpl headers;
  EXPECT_EQ(nullptr, headers.Date());

  provider.setDateHeader(headers);
  EXPECT_EQ(headers.Date()->value().getStringView(), "Thu, 01 Jan 1970 00:00:02 GMT");
}

TEST(SlowDateProviderImplTest, ResponseHeaders) {
  MockTimeSource time_source;
  SystemTime time_point{SystemTime::duration(2000000)};
  EXPECT_CALL(time_source, systemTime()).WillOnce(Return(time_point));

  SlowDateProviderImpl provider(time_source);
  TestResponseHeaderMapImpl headers;
  EXPECT_EQ(nullptr, headers.Date());

  provider.setDateHeader(headers);
  EXPECT_EQ(headers.Date()->value().getStringView(), "Thu, 01 Jan 1970 00:00:02 GMT");
}

TEST(TlsCachingDateProviderImplTest, TlsRequestHeaders) {
  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  Event::MockTimer* timer = new Event::MockTimer(&dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));

  TlsCachingDateProviderImpl provider(dispatcher, tls);
  TestRequestHeaderMapImpl headers;
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());

  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));
  timer->invokeCallback();

  headers.removeDate();
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());
}

TEST(TlsCachingDateProviderImplTest, TlsResponseHeaders) {
  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  Event::MockTimer* timer = new Event::MockTimer(&dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));

  TlsCachingDateProviderImpl provider(dispatcher, tls);
  TestResponseHeaderMapImpl headers;
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());

  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));
  timer->invokeCallback();

  headers.removeDate();
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());
}

} // namespace Http
} // namespace Envoy
