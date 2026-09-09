#include <chrono>
#include <vector>

#include "source/common/http/date_provider_impl.h"
#include "source/common/http/header_map_impl.h"
#include "source/common/thread_local/thread_local_impl.h"

#include "test/mocks/event/mocks.h"
#include "test/mocks/thread_local/mocks.h"
#include "test/test_common/printers.h"
#include "test/test_common/simulated_time_system.h"

#include "gmock/gmock.h"
#include "gtest/gtest.h"

using testing::_;
using testing::AnyNumber;
using testing::StrictMock;

namespace Envoy {
namespace Http {

TEST(DateProviderImplTest, All) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(SystemTime{});
  StrictMock<Event::MockDispatcher> dispatcher;
  StrictMock<ThreadLocal::MockInstance> tls;
  tls.setDispatcher(&dispatcher);
  EXPECT_CALL(tls, allocateSlot());
  auto* timer = new StrictMock<Event::MockTimer>(&dispatcher);
  bool timer_destroyed = false;
  timer->timer_destroyed_ = &timer_destroyed;
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));

  auto provider = std::make_unique<TlsCachingDateProviderImpl>(dispatcher, tls);
  TestResponseHeaderMapImpl headers;
  provider->setDateHeader(headers);
  EXPECT_EQ("Thu, 01 Jan 1970 00:00:00 GMT", headers.getDateValue());

  time_system.setSystemTime(SystemTime{} + std::chrono::seconds(1));
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _));
  timer->invokeCallback();

  headers.removeDate();
  provider->setDateHeader(headers);
  EXPECT_EQ("Thu, 01 Jan 1970 00:00:01 GMT", headers.getDateValue());

  provider.reset();
  EXPECT_TRUE(timer_destroyed);
}

TEST(DateProviderImplTest, RefreshDoesNotQueueUpdatesOnUnstartedWorker) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(SystemTime{});
  StrictMock<Event::MockDispatcher> main_dispatcher;
  StrictMock<Event::MockDispatcher> worker_dispatcher;
  ThreadLocal::InstanceImpl tls;
  std::vector<Event::PostCb> pending_callbacks;
  // Hold worker posts instead of executing them, as during blocked server initialization.
  EXPECT_CALL(worker_dispatcher, post(_)).Times(AnyNumber()).WillRepeatedly([&](Event::PostCb cb) {
    pending_callbacks.push_back(std::move(cb));
  });
  tls.registerThread(main_dispatcher, true);
  tls.registerThread(worker_dispatcher, false);

  auto* timer = new StrictMock<Event::MockTimer>(&main_dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _)).Times(7201);
  TlsCachingDateProviderImpl provider(main_dispatcher, tls);
  // One post registers the worker dispatcher, and one initializes the date cache.
  EXPECT_EQ(2, pending_callbacks.size());
  for (int i = 1; i <= 7200; ++i) {
    time_system.setSystemTime(SystemTime{} + std::chrono::milliseconds(500 * i));
    timer->invokeCallback();
  }
  EXPECT_EQ(2, pending_callbacks.size());
  TestResponseHeaderMapImpl headers;
  provider.setDateHeader(headers);
  EXPECT_EQ("Thu, 01 Jan 1970 01:00:00 GMT", headers.getDateValue());

  tls.shutdownGlobalThreading();
  tls.shutdownThread();
}

TEST(DateProviderImplTest, DeferredInitializationUsesCurrentDate) {
  Event::SimulatedTimeSystem time_system;
  time_system.setSystemTime(SystemTime{});
  StrictMock<Event::MockDispatcher> dispatcher;
  StrictMock<ThreadLocal::MockInstance> tls;
  tls.setDispatcher(&dispatcher);
  tls.defer_data_ = true;
  EXPECT_CALL(tls, allocateSlot());
  TlsCachingDateProviderImpl provider(dispatcher, tls);

  // A delayed worker creates its cache and timer only when it processes initialization.
  time_system.setSystemTime(SystemTime{} + std::chrono::hours(1));
  auto* timer = new StrictMock<Event::MockTimer>(&dispatcher);
  EXPECT_CALL(*timer, enableTimer(std::chrono::milliseconds(500), _)).Times(2);
  tls.call();
  TestResponseHeaderMapImpl headers;
  provider.setDateHeader(headers);
  EXPECT_EQ("Thu, 01 Jan 1970 01:00:00 GMT", headers.getDateValue());

  time_system.setSystemTime(SystemTime{} + std::chrono::hours(1) + std::chrono::seconds(1));
  timer->invokeCallback();
  provider.setDateHeader(headers);
  EXPECT_EQ("Thu, 01 Jan 1970 01:00:01 GMT", headers.getDateValue());
}

} // namespace Http
} // namespace Envoy
