#include <cstdint>
#include <deque>
#include <memory>
#include <string>
#include <utility>

#include "source/common/buffer/buffer_impl.h"
#include "source/common/coroutine/dispatcher_executor.h"
#include "source/common/coroutine/launch.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer.h"
#include "source/extensions/filters/http/ai_protocol_manager/external_buffer_impl.h"
#include "source/extensions/filters/http/ai_protocol_manager/payload_reader.h"

#include "test/test_common/status_utility.h"
#include "test/test_common/utility.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace AiProtocolManager {
namespace {

using ::Envoy::StatusHelpers::HasStatusCode;

// ExternalBuffer double that holds read callbacks until the test completes them and tracks maximum
// concurrent in-flight reads to verify single-flight read serialization.
class ControlledReadExternalBuffer : public ExternalBuffer {
public:
  struct SharedState {
    std::string payload;
    int in_flight_reads{0};
    int max_concurrent_reads{0};
    int total_reads{0};
    std::deque<std::pair<std::string, ReadCallback>> pending_reads;
  };

  explicit ControlledReadExternalBuffer(std::shared_ptr<SharedState> state)
      : state_(std::move(state)) {}

  void write(Buffer::InstancePtr data, bool /*end_stream*/, WriteCallback done) override {
    state_->payload.append(data->toString());
    done(ExternalBufferStatus::Ok);
  }

  void read(uint64_t offset, uint64_t length, ReadCallback done) override {
    ++state_->in_flight_reads;
    ++state_->total_reads;
    if (state_->in_flight_reads > state_->max_concurrent_reads) {
      state_->max_concurrent_reads = state_->in_flight_reads;
    }
    std::string slice = state_->payload.substr(offset, length);
    state_->pending_reads.emplace_back(std::move(slice), std::move(done));
  }

  uint64_t length() const override { return state_->payload.size(); }

private:
  std::shared_ptr<SharedState> state_;
};

class PayloadReaderTest : public testing::Test {
protected:
  PayloadReaderTest()
      : api_(Api::createApiForTest()), dispatcher_(api_->allocateDispatcher("test")),
        executor_(std::make_shared<Coroutine::DispatcherExecutor>(*dispatcher_)) {}

  void drain() { dispatcher_->run(Event::Dispatcher::RunType::Block); }

  Api::ApiPtr api_;
  Event::DispatcherPtr dispatcher_;
  std::shared_ptr<Coroutine::DispatcherExecutor> executor_;
};

TEST_F(PayloadReaderTest, FromInMemoryReadsSliceWithoutSuspending) {
  Buffer::OwnedImpl mem("0123456789");
  PayloadReaderSharedPtr reader = PayloadReader::fromInMemory(mem);

  EXPECT_EQ(reader->length(), 10);
  EXPECT_TRUE(reader->allBytesReadable());

  absl::StatusOr<Buffer::InstancePtr> chunk_or = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(2, 4), executor_,
      [&chunk_or](absl::StatusOr<Buffer::InstancePtr> res) { chunk_or = std::move(res); },
      Coroutine::StartMode::Inline);

  // In-memory readChunk resolves synchronously without dispatcher pumping.
  ASSERT_OK(chunk_or);
  ASSERT_NE(*chunk_or, nullptr);
  EXPECT_EQ((*chunk_or)->toString(), "2345");

  // Zero-length read also resolves synchronously with an empty buffer.
  absl::StatusOr<Buffer::InstancePtr> empty_or = absl::UnknownError("pending");
  auto empty_handle = Coroutine::launch(
      reader->readChunk(5, 0), executor_,
      [&empty_or](absl::StatusOr<Buffer::InstancePtr> res) { empty_or = std::move(res); },
      Coroutine::StartMode::Inline);
  ASSERT_OK(empty_or);
  ASSERT_NE(*empty_or, nullptr);
  EXPECT_EQ((*empty_or)->length(), 0);
}

TEST_F(PayloadReaderTest, CreateFailedRejectsReadsImmediately) {
  PayloadReaderSharedPtr reader = PayloadReader::createFailed(16);
  EXPECT_EQ(reader->length(), 16);
  EXPECT_TRUE(reader->hasFailed());
  EXPECT_FALSE(reader->allBytesReadable());

  // Subsequent handoff calls on an already-failed reader are no-ops.
  reader->failExternalHandoff();
  reader->completeExternalHandoff(nullptr);
  EXPECT_TRUE(reader->hasFailed());

  absl::StatusOr<Buffer::InstancePtr> chunk_or = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(0, 4), executor_,
      [&chunk_or](absl::StatusOr<Buffer::InstancePtr> res) { chunk_or = std::move(res); },
      Coroutine::StartMode::Inline);
  EXPECT_THAT(chunk_or.status(), HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(PayloadReaderTest, PendingExternalHandoffWaitsUntilCompletedAndReadsSlice) {
  PayloadReaderSharedPtr reader = PayloadReader::createPendingExternal(10);
  EXPECT_FALSE(reader->allBytesReadable());

  absl::StatusOr<Buffer::InstancePtr> chunk_or = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(3, 5), executor_,
      [&chunk_or](absl::StatusOr<Buffer::InstancePtr> res) { chunk_or = std::move(res); },
      Coroutine::StartMode::Inline);
  // Zero-length reads resolve synchronously without waiting for external handoff.
  absl::StatusOr<Buffer::InstancePtr> empty_or = absl::UnknownError("pending");
  auto empty_handle = Coroutine::launch(
      reader->readChunk(0, 0), executor_,
      [&empty_or](absl::StatusOr<Buffer::InstancePtr> res) { empty_or = std::move(res); },
      Coroutine::StartMode::Inline);
  ASSERT_OK(empty_or);
  ASSERT_NE(*empty_or, nullptr);
  EXPECT_EQ((*empty_or)->length(), 0);

  drain();
  EXPECT_FALSE(chunk_or.ok());
  EXPECT_EQ(chunk_or.status().code(), absl::StatusCode::kUnknown);

  InMemoryExternalBufferFactory factory;
  std::shared_ptr<ExternalBuffer> ext = factory.createBuffer(*dispatcher_);
  ext->write(std::make_unique<Buffer::OwnedImpl>("0123456789"), /*end_stream=*/true,
             [](ExternalBufferStatus) {});
  drain();

  reader->completeExternalHandoff(std::move(ext));
  drain();

  ASSERT_OK(chunk_or);
  ASSERT_NE(*chunk_or, nullptr);
  EXPECT_EQ((*chunk_or)->toString(), "34567");

  // Subsequent failExternalHandoff() after completion is a no-op.
  reader->failExternalHandoff();
  EXPECT_FALSE(reader->hasFailed());
  EXPECT_TRUE(reader->allBytesReadable());
}

TEST_F(PayloadReaderTest, PendingExternalHandoffFailureFailsWaitersAndSubsequentReads) {
  PayloadReaderSharedPtr reader = PayloadReader::createPendingExternal(10);

  absl::StatusOr<Buffer::InstancePtr> wait_res = absl::UnknownError("pending");
  auto wait_handle = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&wait_res](absl::StatusOr<Buffer::InstancePtr> res) { wait_res = std::move(res); },
      Coroutine::StartMode::Inline);

  reader->failExternalHandoff();
  drain();
  EXPECT_THAT(wait_res.status(), HasStatusCode(absl::StatusCode::kDataLoss));
  EXPECT_TRUE(reader->hasFailed());

  absl::StatusOr<Buffer::InstancePtr> read1 = absl::UnknownError("pending");
  auto h1 = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&read1](absl::StatusOr<Buffer::InstancePtr> res) { read1 = std::move(res); },
      Coroutine::StartMode::Inline);
  absl::StatusOr<Buffer::InstancePtr> read2 = absl::UnknownError("pending");
  auto h2 = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&read2](absl::StatusOr<Buffer::InstancePtr> res) { read2 = std::move(res); },
      Coroutine::StartMode::Inline);
  drain();

  EXPECT_THAT(read1.status(), HasStatusCode(absl::StatusCode::kDataLoss));
  EXPECT_THAT(read2.status(), HasStatusCode(absl::StatusCode::kDataLoss));
}

TEST_F(PayloadReaderTest, ContextCancelUnwindsWaitReadableWithoutPoisoningReader) {
  PayloadReaderSharedPtr reader = PayloadReader::createPendingExternal(10);

  absl::StatusOr<Buffer::InstancePtr> wait_res = absl::UnknownError("pending");
  auto wait_handle = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&wait_res](absl::StatusOr<Buffer::InstancePtr> res) { wait_res = std::move(res); },
      Coroutine::StartMode::Inline);

  wait_handle.cancel();
  EXPECT_THAT(wait_res.status(), HasStatusCode(absl::StatusCode::kCancelled));
  EXPECT_FALSE(reader->hasFailed());

  InMemoryExternalBufferFactory factory;
  std::shared_ptr<ExternalBuffer> ext = factory.createBuffer(*dispatcher_);
  ext->write(std::make_unique<Buffer::OwnedImpl>("0123456789"), /*end_stream=*/true,
             [](ExternalBufferStatus) {});
  drain();
  reader->completeExternalHandoff(std::move(ext));
  EXPECT_TRUE(reader->allBytesReadable());
}

TEST_F(PayloadReaderTest, OutOfBoundsReadChunkReturnsInvalidArgument) {
  Buffer::OwnedImpl mem("hello");
  PayloadReaderSharedPtr reader = PayloadReader::fromInMemory(mem);

  absl::StatusOr<Buffer::InstancePtr> chunk_or = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(3, 4), executor_,
      [&chunk_or](absl::StatusOr<Buffer::InstancePtr> res) { chunk_or = std::move(res); },
      Coroutine::StartMode::Inline);
  drain();

  EXPECT_THAT(chunk_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

// Verifies single-flight read safety: if Attempt 1 cancels mid-read while its ExternalBuffer read
// is still in flight, Attempt 2 waits on the semaphore until Attempt 1's underlying read callback
// finishes rather than issuing an overlapping read().
TEST_F(PayloadReaderTest, SingleFlightSemaphoreSerializesReadsAcrossMidReadCancellation) {
  auto state = std::make_shared<ControlledReadExternalBuffer::SharedState>();
  state->payload = "0123456789";
  auto ext = std::make_shared<ControlledReadExternalBuffer>(state);
  PayloadReaderSharedPtr reader = PayloadReader::fromExternalBuffer(std::move(ext), 10);

  absl::StatusOr<Buffer::InstancePtr> res1 = absl::UnknownError("pending1");
  auto handle1 = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&res1](absl::StatusOr<Buffer::InstancePtr> r) { res1 = std::move(r); },
      Coroutine::StartMode::Inline);

  absl::StatusOr<Buffer::InstancePtr> res2 = absl::UnknownError("pending2");
  auto handle2 = Coroutine::launch(
      reader->readChunk(5, 5), executor_,
      [&res2](absl::StatusOr<Buffer::InstancePtr> r) { res2 = std::move(r); },
      Coroutine::StartMode::Inline);

  drain();
  EXPECT_EQ(state->in_flight_reads, 1);
  EXPECT_EQ(state->total_reads, 1);

  // Cancel Attempt 1 while its ExternalBuffer::read() is still in flight.
  handle1.cancel();
  drain();
  EXPECT_THAT(res1.status(), HasStatusCode(absl::StatusCode::kCancelled));
  // Attempt 2 must not start its read() until Attempt 1's underlying read callback returns.
  EXPECT_EQ(state->in_flight_reads, 1);
  EXPECT_EQ(state->total_reads, 1);
  EXPECT_EQ(state->max_concurrent_reads, 1);

  // Finish Attempt 1's underlying read callback -> releases the semaphore -> Attempt 2 starts.
  {
    auto [slice, done] = std::move(state->pending_reads.front());
    state->pending_reads.pop_front();
    --state->in_flight_reads;
    std::move(done)(ExternalBufferStatus::Ok, std::make_unique<Buffer::OwnedImpl>(slice));
  }
  drain();
  EXPECT_EQ(state->in_flight_reads, 1);
  EXPECT_EQ(state->total_reads, 2);
  EXPECT_EQ(state->max_concurrent_reads, 1);

  // Finish Attempt 2's read callback.
  {
    auto [slice, done] = std::move(state->pending_reads.front());
    state->pending_reads.pop_front();
    --state->in_flight_reads;
    std::move(done)(ExternalBufferStatus::Ok, std::make_unique<Buffer::OwnedImpl>(slice));
  }
  drain();

  ASSERT_OK(res2);
  ASSERT_NE(*res2, nullptr);
  EXPECT_EQ((*res2)->toString(), "56789");
  EXPECT_EQ(state->max_concurrent_reads, 1);
}

TEST_F(PayloadReaderTest, ExternalReadFailureMarksReaderFailedAndFailsSubsequentReads) {
  auto state = std::make_shared<ControlledReadExternalBuffer::SharedState>();
  state->payload = "0123456789";
  auto ext = std::make_shared<ControlledReadExternalBuffer>(state);
  PayloadReaderSharedPtr reader = PayloadReader::fromExternalBuffer(std::move(ext), 10);
  EXPECT_FALSE(reader->hasFailed());

  absl::StatusOr<Buffer::InstancePtr> res1 = absl::UnknownError("pending1");
  auto handle1 = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&res1](absl::StatusOr<Buffer::InstancePtr> r) { res1 = std::move(r); },
      Coroutine::StartMode::Inline);
  // Park a second reader on the semaphore so it observes failed_ immediately upon wake-up.
  absl::StatusOr<Buffer::InstancePtr> queued = absl::UnknownError("queued");
  auto queued_handle = Coroutine::launch(
      reader->readChunk(5, 5), executor_,
      [&queued](absl::StatusOr<Buffer::InstancePtr> r) { queued = std::move(r); },
      Coroutine::StartMode::Inline);
  drain();

  {
    auto [slice, done] = std::move(state->pending_reads.front());
    state->pending_reads.pop_front();
    --state->in_flight_reads;
    std::move(done)(ExternalBufferStatus::Error, nullptr);
  }
  drain();

  EXPECT_THAT(res1.status(), HasStatusCode(absl::StatusCode::kDataLoss));
  EXPECT_THAT(queued.status(), HasStatusCode(absl::StatusCode::kDataLoss));
  EXPECT_TRUE(reader->hasFailed());
  EXPECT_FALSE(reader->allBytesReadable());

  absl::StatusOr<Buffer::InstancePtr> res2 = absl::UnknownError("pending2");
  auto handle2 = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&res2](absl::StatusOr<Buffer::InstancePtr> r) { res2 = std::move(r); },
      Coroutine::StartMode::Inline);
  drain();
  EXPECT_THAT(res2.status(), HasStatusCode(absl::StatusCode::kDataLoss));
  EXPECT_EQ(state->total_reads, 1);
}

// Cancelling an in-flight read and dropping the last external PayloadReaderSharedPtr must not leak
// via a PayloadReader <-> ExternalBuffer cycle; ExternalBuffer must destruct immediately.
TEST_F(PayloadReaderTest, CancelMidReadAndDropLastRefDestroysExternalBufferImmediately) {
  class PendingCallbackExternalBuffer : public ExternalBuffer {
  public:
    void write(Buffer::InstancePtr, bool, WriteCallback done) override {
      done(ExternalBufferStatus::Ok);
    }
    void read(uint64_t, uint64_t, ReadCallback done) override { pending_read_ = std::move(done); }
    uint64_t length() const override { return 10; }

    ReadCallback pending_read_;
  };

  auto ext = std::make_shared<PendingCallbackExternalBuffer>();
  std::weak_ptr<ExternalBuffer> weak_ext = ext;
  PayloadReaderSharedPtr reader = PayloadReader::fromExternalBuffer(std::move(ext), 10);
  std::weak_ptr<PayloadReader> weak_reader = reader;

  absl::StatusOr<Buffer::InstancePtr> res = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(0, 5), executor_,
      [&res](absl::StatusOr<Buffer::InstancePtr> r) { res = std::move(r); },
      Coroutine::StartMode::Inline);
  drain();

  handle.cancel();
  EXPECT_THAT(res.status(), HasStatusCode(absl::StatusCode::kCancelled));

  reader.reset();
  EXPECT_TRUE(weak_reader.expired());
  EXPECT_TRUE(weak_ext.expired());
}

TEST_F(PayloadReaderTest, OverflowReadRangeReturnsInvalidArgument) {
  Buffer::OwnedImpl mem("hello");
  PayloadReaderSharedPtr reader = PayloadReader::fromInMemory(mem);

  absl::StatusOr<Buffer::InstancePtr> chunk_or = absl::UnknownError("pending");
  auto handle = Coroutine::launch(
      reader->readChunk(UINT64_MAX, 1), executor_,
      [&chunk_or](absl::StatusOr<Buffer::InstancePtr> res) { chunk_or = std::move(res); },
      Coroutine::StartMode::Inline);
  EXPECT_THAT(chunk_or.status(), HasStatusCode(absl::StatusCode::kInvalidArgument));
}

} // namespace
} // namespace AiProtocolManager
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
