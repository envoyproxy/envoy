#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"

#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

class InMemoryExternalBufferTest : public testing::Test {
public:
  InMemoryExternalBufferTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        buffer_(*dispatcher_) {}

  void drain() { dispatcher_->run(Event::Dispatcher::RunType::NonBlock); }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  InMemoryExternalBuffer buffer_;
};

// Appends are asynchronous: nothing is durable until the event loop runs.
TEST_F(InMemoryExternalBufferTest, AppendIsAsynchronous) {
  bool acked = false;
  buffer_.append(std::make_unique<Buffer::OwnedImpl>("hello"),
                 [&acked](ExternalBufferStatus status) {
                   EXPECT_EQ(status, ExternalBufferStatus::Ok);
                   acked = true;
                 });

  // The write is not durable until the event loop runs.
  EXPECT_FALSE(acked);
  EXPECT_EQ(buffer_.length(), 0);

  drain();

  EXPECT_TRUE(acked);
  EXPECT_EQ(buffer_.length(), 5);
}

// Multiple appends accumulate in order and read-back returns the bytes verbatim.
TEST_F(InMemoryExternalBufferTest, AppendThenReadRoundTrips) {
  for (absl::string_view chunk : {"abc", "def", "ghij"}) {
    buffer_.append(std::make_unique<Buffer::OwnedImpl>(chunk), [](ExternalBufferStatus status) {
      EXPECT_EQ(status, ExternalBufferStatus::Ok);
    });
  }
  drain();
  EXPECT_EQ(buffer_.length(), 10);

  std::string read_back;
  buffer_.read(0, buffer_.length(),
               [&read_back](ExternalBufferStatus status, Buffer::InstancePtr data) {
                 EXPECT_EQ(status, ExternalBufferStatus::Ok);
                 read_back = data->toString();
               });
  EXPECT_TRUE(read_back.empty()); // read is async too.
  drain();
  EXPECT_EQ(read_back, "abcdefghij");
}

// Reads honor arbitrary byte offsets and are non-destructive (repeatable).
TEST_F(InMemoryExternalBufferTest, ReadAtOffsetIsRepeatable) {
  buffer_.append(std::make_unique<Buffer::OwnedImpl>("0123456789"), [](ExternalBufferStatus) {});
  drain();

  std::vector<std::string> results;
  auto collect = [&results](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    results.push_back(data->toString());
  };
  buffer_.read(3, 4, collect);
  buffer_.read(3, 4, collect);
  drain();

  ASSERT_EQ(results.size(), 2);
  EXPECT_EQ(results[0], "3456");
  EXPECT_EQ(results[1], "3456");
  EXPECT_EQ(buffer_.length(), 10); // unchanged by reads.
}

// Destroying the buffer with callbacks still queued must not invoke them.
TEST_F(InMemoryExternalBufferTest, PendingCallbacksCancelledOnDestruction) {
  bool acked = false;
  {
    InMemoryExternalBuffer scoped(*dispatcher_);
    scoped.append(std::make_unique<Buffer::OwnedImpl>("data"),
                  [&acked](ExternalBufferStatus) { acked = true; });
    // scoped goes out of scope before the dispatcher runs.
  }
  drain();
  EXPECT_FALSE(acked);
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
