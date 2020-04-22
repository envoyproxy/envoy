#include <chrono>

#include "common/http/date_provider_impl.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::NiceMock;

namespace Envoy {
namespace Http {

TEST(DateProviderImplTest, All) {
  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  Event::MockTimer* timer = new Event::MockTimer(&dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));

  TlsCachingDateProviderImpl provider(dispatcher, tls);
  ResponseHeaderMapImpl headers;
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
