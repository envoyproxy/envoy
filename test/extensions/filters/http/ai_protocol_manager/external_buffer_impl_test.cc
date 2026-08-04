#include <memory>
#include <string>
#include <vector>

#include "source/common/buffer/buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"

#include "test/test_common/utility.h"

#include "gmock/gmock.h"
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

// Writes are asynchronous: nothing is durable until the event loop runs.
TEST_F(InMemoryExternalBufferTest, WriteIsAsynchronous) {
  bool acked = false;
  buffer_.write(std::make_unique<Buffer::OwnedImpl>("hello"),
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

// Successive writes accumulate in order and read-back returns the bytes verbatim.
TEST_F(InMemoryExternalBufferTest, WriteThenReadRoundTrips) {
  for (absl::string_view chunk : {"abc", "def", "ghij"}) {
    buffer_.write(std::make_unique<Buffer::OwnedImpl>(chunk),
                  [](ExternalBufferStatus status) { EXPECT_EQ(status, ExternalBufferStatus::Ok); });
  }
  drain();
  EXPECT_EQ(buffer_.length(), 10);

  std::string read_back;
  buffer_.read(0, buffer_.length(),
               [&read_back](ExternalBufferStatus status, Buffer::InstancePtr data) {
                 EXPECT_EQ(status, ExternalBufferStatus::Ok);
                 read_back = data->toString();
               });
  // Reads complete synchronously: the callback has already run.
  EXPECT_EQ(read_back, "abcdefghij");
}

// Reads honor arbitrary byte offsets and are non-destructive (repeatable).
TEST_F(InMemoryExternalBufferTest, ReadAtOffsetIsRepeatable) {
  buffer_.write(std::make_unique<Buffer::OwnedImpl>("0123456789"), [](ExternalBufferStatus) {});
  drain();

  std::vector<std::string> results;
  auto collect = [&results](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    results.push_back(data->toString());
  };
  // Reads complete synchronously, so results are populated immediately.
  buffer_.read(3, 4, collect);
  buffer_.read(3, 4, collect);

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
    scoped.write(std::make_unique<Buffer::OwnedImpl>("data"),
                 [&acked](ExternalBufferStatus) { acked = true; });
    // scoped goes out of scope before the dispatcher runs.
  }
  drain();
  EXPECT_FALSE(acked);
}

// Several writes with pending completions are all cancelled together when the
// buffer is destroyed -- none of the callbacks fire.
TEST_F(InMemoryExternalBufferTest, AllPendingWritesCancelledOnDestruction) {
  int acked = 0;
  {
    InMemoryExternalBuffer scoped(*dispatcher_);
    for (int i = 0; i < 3; ++i) {
      scoped.write(std::make_unique<Buffer::OwnedImpl>("x"),
                   [&acked](ExternalBufferStatus) { ++acked; });
    }
  }
  drain();
  EXPECT_EQ(acked, 0);
}

// The read callback runs on-stack, before read() returns -- the synchronous
// completion the BufferManager relies on for its re-entrant replay burst.
TEST_F(InMemoryExternalBufferTest, ReadInvokesCallbackBeforeReturning) {
  buffer_.write(std::make_unique<Buffer::OwnedImpl>("0123456789"), [](ExternalBufferStatus) {});
  drain();

  bool ran = false;
  buffer_.read(0, 4, [&ran](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    EXPECT_EQ(data->toString(), "0123");
    ran = true;
  });
  // No drain(): the callback has already run by the time read() returns.
  EXPECT_TRUE(ran);
}

// A zero-length read is valid (e.g. the empty-body replay path) and yields a
// non-null, empty buffer -- both on a fresh buffer and at an interior offset.
TEST_F(InMemoryExternalBufferTest, ZeroLengthReadReturnsEmptyBuffer) {
  bool ran = false;
  buffer_.read(0, 0, [&ran](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    ASSERT_NE(data, nullptr);
    EXPECT_EQ(data->length(), 0);
    ran = true;
  });
  EXPECT_TRUE(ran);

  buffer_.write(std::make_unique<Buffer::OwnedImpl>("data"), [](ExternalBufferStatus) {});
  drain();
  ran = false;
  buffer_.read(2, 0, [&ran](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    EXPECT_EQ(data->length(), 0);
    ran = true;
  });
  EXPECT_TRUE(ran);
  EXPECT_EQ(buffer_.length(), 4); // Unchanged by the reads.
}

// An empty write still completes (asynchronously) with Ok and leaves length at 0.
TEST_F(InMemoryExternalBufferTest, EmptyWriteIsAcknowledged) {
  bool acked = false;
  buffer_.write(std::make_unique<Buffer::OwnedImpl>(), [&acked](ExternalBufferStatus status) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    acked = true;
  });
  EXPECT_FALSE(acked);
  drain();
  EXPECT_TRUE(acked);
  EXPECT_EQ(buffer_.length(), 0);
}

// Posted write completions fire in submission order, and the bytes land in the
// same order.
TEST_F(InMemoryExternalBufferTest, WriteCompletionsFireInOrder) {
  std::vector<int> order;
  for (int i = 0; i < 3; ++i) {
    buffer_.write(std::make_unique<Buffer::OwnedImpl>(std::to_string(i)),
                  [&order, i](ExternalBufferStatus status) {
                    EXPECT_EQ(status, ExternalBufferStatus::Ok);
                    order.push_back(i);
                  });
  }
  drain();
  EXPECT_THAT(order, testing::ElementsAre(0, 1, 2));

  std::string read_back;
  buffer_.read(0, buffer_.length(), [&read_back](ExternalBufferStatus, Buffer::InstancePtr data) {
    read_back = data->toString();
  });
  EXPECT_EQ(read_back, "012");
}

// length() reflects only acknowledged (durable) writes; an issued-but-not-yet-acked
// write does not count until the event loop runs. A read may then span writes and
// reach the exact end boundary (offset + length == length()).
TEST_F(InMemoryExternalBufferTest, LengthReflectsOnlyDurableWrites) {
  buffer_.write(std::make_unique<Buffer::OwnedImpl>("aaaa"), [](ExternalBufferStatus) {});
  EXPECT_EQ(buffer_.length(), 0); // Not yet acknowledged.
  drain();
  EXPECT_EQ(buffer_.length(), 4);

  buffer_.write(std::make_unique<Buffer::OwnedImpl>("bb"), [](ExternalBufferStatus) {});
  EXPECT_EQ(buffer_.length(), 4); // Second write still in flight.
  drain();
  EXPECT_EQ(buffer_.length(), 6);

  std::string read_back;
  buffer_.read(3, 3, [&read_back](ExternalBufferStatus status, Buffer::InstancePtr data) {
    EXPECT_EQ(status, ExternalBufferStatus::Ok);
    read_back = data->toString();
  });
  EXPECT_EQ(read_back, "abb"); // Spans the two writes, up to the end.
}

// Reading past the acknowledged length violates the precondition and trips the
// debug assertion.
TEST_F(InMemoryExternalBufferTest, ReadPastLengthIsDebugDeath) {
  buffer_.write(std::make_unique<Buffer::OwnedImpl>("short"), [](ExternalBufferStatus) {});
  drain();
  ASSERT_EQ(buffer_.length(), 5);
  EXPECT_DEBUG_DEATH(buffer_.read(0, 6, [](ExternalBufferStatus, Buffer::InstancePtr) {}),
                     "offset \\+ length <= data_");
}

// The factory produces a usable, independent buffer.
TEST_F(InMemoryExternalBufferTest, FactoryCreatesUsableBuffer) {
  InMemoryExternalBufferFactory factory;
  ExternalBufferPtr buffer = factory.createBuffer(*dispatcher_);
  ASSERT_NE(buffer, nullptr);

  buffer->write(std::make_unique<Buffer::OwnedImpl>("via-factory"), [](ExternalBufferStatus) {});
  drain();
  EXPECT_EQ(buffer->length(), 11);

  std::string read_back;
  buffer->read(0, buffer->length(),
               [&read_back](ExternalBufferStatus status, Buffer::InstancePtr data) {
                 EXPECT_EQ(status, ExternalBufferStatus::Ok);
                 read_back = data->toString();
               });
  EXPECT_EQ(read_back, "via-factory");
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
