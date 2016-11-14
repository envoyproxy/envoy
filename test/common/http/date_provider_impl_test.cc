#include "common/http/date_provider_impl.h"
#include "common/http/header_map_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"

using testing::NiceMock;

namespace Http {

TEST(DateProviderImplTest, All) {
  Event::MockDispatcher dispatcher;
  NiceMock<ThreadLocal::MockInstance> tls;
  Event::MockTimer* timer = new Event::MockTimer(&dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500)));

  TlsCachingDateProviderImpl provider(dispatcher, tls);
  HeaderMapImpl headers;
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());

  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500)));
  timer->callback_();

  headers.removeDate();
  provider.setDateHeader(headers);
  EXPECT_NE(nullptr, headers.Date());
}

} // Http
