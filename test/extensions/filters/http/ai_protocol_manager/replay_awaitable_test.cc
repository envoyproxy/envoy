#include <memory>
#include <string>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/extensions/filters/http/ai_protocol_manager/buffer_manager.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/replay_awaitable.h"

#include "test/extensions/filters/http/ai_protocol_manager/fake_bridge.h"
#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::IsOk;

class ReplayAwaitableTest : public testing::Test {
protected:
  ReplayAwaitableTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        bridge_(*dispatcher_), manager_(BufferManager::Config{}, factory_, bridge_),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)) {}

  void drain() {
    for (int i = 0; i < 40; ++i) {
      dispatcher_->run(Event::Dispatcher::RunType::NonBlock);
    }
  }

  // Fills the store and closes it, which is what a range replay waits for.
  void store(absl::string_view bytes) {
    Buffer::OwnedImpl buf;
    buf.add(bytes);
    manager_.onData(buf);
    manager_.endStream();
  }

  Coroutine::Task<absl::Status> replay(uint64_t offset, uint64_t length) {
    co_return co_await ReplayAwaitable(manager_, offset, length);
  }

  Coroutine::Task<absl::Status> inject(Buffer::Instance& data) {
    co_return co_await ReplayAwaitable(manager_, data);
  }

  Coroutine::DetachedHandle launch(Coroutine::Task<absl::Status> task) {
    return Coroutine::launch(
        std::move(task), executor_, [this](absl::Status status) { result_ = std::move(status); },
        Coroutine::StartMode::Inline);
  }

  std::string injected() { return bridge_.injected_.toString(); }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBufferFactory factory_;
  // Declared before manager_ so it outlives the manager that references it.
  FakeBridge bridge_;
  BufferManager manager_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
  absl::Status result_ = absl::UnknownError("never completed");
};

TEST_F(ReplayAwaitableTest, ReplaysARangeOfTheStore) {
  store("0123456789");
  Coroutine::DetachedHandle handle = launch(replay(2, 4));
  drain();

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "2345");
}

TEST_F(ReplayAwaitableTest, InjectsTheCallersBytesAndDrainsThem) {
  Buffer::OwnedImpl data;
  data.add("hello");
  Coroutine::DetachedHandle handle = launch(inject(data));
  drain();

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "hello");
  EXPECT_EQ(data.length(), 0) << "the caller's buffer was not drained";
}

// Nothing to write means nothing to wait for: the await resolves on the caller's stack, without a
// trip through the dispatcher. Asserting before drain() is what makes that visible.
TEST_F(ReplayAwaitableTest, EmptyRangeCompletesWithoutSuspending) {
  Coroutine::DetachedHandle handle = launch(replay(0, 0));

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "");
}

TEST_F(ReplayAwaitableTest, EmptyInjectCompletesWithoutSuspending) {
  Buffer::OwnedImpl data;
  Coroutine::DetachedHandle handle = launch(inject(data));

  EXPECT_THAT(result_, IsOk());
  EXPECT_EQ(injected(), "");
}

// Cancelling the coroutine has to reach the BufferManager, not just abandon the await: the manager
// would otherwise keep injecting into a filter chain whose producer is gone.
TEST_F(ReplayAwaitableTest, CancelStopsAPendingRangeReplay) {
  store("0123456789");
  Coroutine::DetachedHandle handle = launch(replay(0, 10));
  handle.cancel();
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(injected(), "");
}

TEST_F(ReplayAwaitableTest, CancelStopsAPendingInject) {
  Buffer::OwnedImpl data;
  data.add("hello");
  Coroutine::DetachedHandle handle = launch(inject(data));
  handle.cancel();
  drain();

  EXPECT_EQ(result_.code(), absl::StatusCode::kCancelled);
  EXPECT_EQ(injected(), "");
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
