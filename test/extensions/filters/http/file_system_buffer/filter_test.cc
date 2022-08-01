#include <functional>
#include <memory>
#include <vector>

#include "source/extensions/filters/http/file_system_buffer/filter.h"

#include "test/extensions/common/async_files/mocks.h"
#include "test/mocks/buffer/mocks.h"
#include "test/mocks/event/mocks.h"
#include "test/mocks/server/mocks.h"
#include "test/mocks/upstream/mocks.h"

#include "gtest/gtest.h"

namespace Envoy {
namespace Extensions {
namespace HttpFilters {
namespace FileSystemBuffer {

using Extensions::Common::AsyncFiles::MockAsyncFileContext;
using Extensions::Common::AsyncFiles::MockAsyncFileHandle;
using Extensions::Common::AsyncFiles::MockAsyncFileManager;
using Extensions::Common::AsyncFiles::MockAsyncFileManagerFactory;
using ::testing::NiceMock;
using ::testing::Return;

class FileSystemBufferFilterTest : public testing::Test {
public:
  void SetUp() override {
    ON_CALL(*mock_file_manager_factory_, getAsyncFileManager(_, _))
        .WillByDefault(Return(mock_async_file_manager_));
  }

  void TearDown() override { destroyFilter(); }

  void destroyFilter() {
    if (filter_) {
      filter_->onDestroy();
      filter_.reset();
    }
  }

protected:
  void expectAsyncFileCreated() {
    EXPECT_CALL(*mock_async_file_manager_, createAnonymousFile(_, _));
  }
  // A file is only opened by the filter if it's going to chain a write immediately afterwards,
  // so we combine completing the open and expecting the write, which allows for simplified control
  // of the handle.
  MockAsyncFileHandle completeCreateFileAndExpectWrite(absl::string_view content) {
    auto handle =
        std::make_shared<testing::StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
    expectWriteWithPosition(handle, content, 0);
    mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{handle});
    return handle;
  }
  void expectWriteWithPosition(MockAsyncFileHandle handle, absl::string_view content,
                               off_t offset) {
    EXPECT_CALL(*handle, write(BufferStringEqual(std::string(content)), offset, _));
  }
  void completeWriteOfSize(size_t length) {
    mock_async_file_manager_->nextActionCompletes(absl::StatusOr<size_t>{length});
  }
  void expectRead(MockAsyncFileHandle handle, off_t offset, size_t size) {
    EXPECT_CALL(*handle, read(offset, size, _));
  }
  void completeRead(absl::string_view content) {
    mock_async_file_manager_->nextActionCompletes(absl::StatusOr<std::unique_ptr<Buffer::Instance>>{
        std::make_unique<Buffer::OwnedImpl>(content)});
  }
  static ProtoFileSystemBufferFilterConfig configProtoFromYaml(absl::string_view yaml) {
    std::string s(yaml);
    ProtoFileSystemBufferFilterConfig config;
    if (!s.empty()) {
      TestUtility::loadFromYaml(s, config);
    }
    return config;
  }

  std::shared_ptr<FileSystemBufferFilterConfig> configFromYaml(absl::string_view yaml) {
    auto proto_config = configProtoFromYaml(yaml);
    return std::make_shared<FileSystemBufferFilterConfig>(mock_file_manager_factory_,
                                                          proto_config.has_manager_config()
                                                              ? mock_async_file_manager_
                                                              : std::shared_ptr<AsyncFileManager>(),
                                                          proto_config);
  }

  void createFilterFromYaml(absl::string_view yaml) {
    auto config = configFromYaml(yaml);
    filter_ = std::make_shared<FileSystemBufferFilter>(config);
    // By default return empty route so we use default config.
    ON_CALL(decoder_callbacks_, route())
        .WillByDefault(Return(std::shared_ptr<Router::MockRoute>()));
    ON_CALL(decoder_callbacks_, injectDecodedDataToFilterChain(_, _))
        .WillByDefault([this](Buffer::Instance& out, bool) { request_sent_on_ += out.toString(); });
    ON_CALL(encoder_callbacks_, injectEncodedDataToFilterChain(_, _))
        .WillByDefault(
            [this](Buffer::Instance& out, bool) { response_sent_on_ += out.toString(); });
    ON_CALL(decoder_callbacks_, continueDecoding()).WillByDefault([this]() {
      ASSERT_FALSE(continued_decoding_);
      continued_decoding_ = true;
    });
    ON_CALL(encoder_callbacks_, continueEncoding()).WillByDefault([this]() {
      ASSERT_FALSE(continued_encoding_);
      continued_encoding_ = true;
    });
    // Using EXPECT_CALL rather than ON_CALL because this one was set up in Envoy code and
    // isn't a NiceMock. Using EXPECT reduces log noise.
    // For simplicity's sake the dispatcher just runs the function immediately
    // - since the mock is capturing callbacks, we're effectively triggering
    // the dispatcher when we resolve the callback.
    EXPECT_CALL(decoder_callbacks_.dispatcher_, post(_))
        .WillRepeatedly([](std::function<void()> fn) { fn(); });
    filter_->setDecoderFilterCallbacks(decoder_callbacks_);
    filter_->setEncoderFilterCallbacks(encoder_callbacks_);
    EXPECT_CALL(decoder_callbacks_, onDecoderFilterAboveWriteBufferHighWatermark())
        .WillRepeatedly([this]() { request_source_watermark_++; });
    EXPECT_CALL(decoder_callbacks_, onDecoderFilterBelowWriteBufferLowWatermark())
        .WillRepeatedly([this]() { request_source_watermark_--; });
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterAboveWriteBufferHighWatermark())
        .WillRepeatedly([this]() {
          filter_->onAboveWriteBufferHighWatermark();
          response_source_watermark_++;
        });
    EXPECT_CALL(encoder_callbacks_, onEncoderFilterBelowWriteBufferLowWatermark())
        .WillRepeatedly([this]() {
          filter_->onBelowWriteBufferLowWatermark();
          response_source_watermark_--;
        });
  }

  void sendResponseHighWatermark() {
    encoder_callbacks_.onEncoderFilterAboveWriteBufferHighWatermark();
  }
  void sendResponseLowWatermark() {
    encoder_callbacks_.onEncoderFilterBelowWriteBufferLowWatermark();
  }
  std::shared_ptr<FileSystemBufferFilter> filter_;
  std::shared_ptr<MockAsyncFileManagerFactory> mock_file_manager_factory_ =
      std::make_shared<NiceMock<MockAsyncFileManagerFactory>>();
  std::shared_ptr<MockAsyncFileManager> mock_async_file_manager_ =
      std::make_shared<NiceMock<MockAsyncFileManager>>();
  NiceMock<Http::MockStreamDecoderFilterCallbacks> decoder_callbacks_;
  NiceMock<Http::MockStreamEncoderFilterCallbacks> encoder_callbacks_;
  Http::TestRequestHeaderMapImpl request_headers_ = {{":method", "GET"}, {":path", "/"}};
  Http::TestResponseHeaderMapImpl response_headers_ = {};
  Http::TestRequestTrailerMapImpl request_trailers_ = {{"some_request_trailer", "beep"}};
  Http::TestResponseTrailerMapImpl response_trailers_ = {{"some_response_trailer", "boop"}};
  std::string request_sent_on_;
  std::string response_sent_on_;
  bool continued_decoding_ = false;
  bool continued_encoding_ = false;
  int request_source_watermark_ = 0;
  int response_source_watermark_ = 0;
  LogLevelSetter log_level_setter_ = LogLevelSetter(ENVOY_SPDLOG_LEVEL(debug));
};

constexpr char minimal_config[] = R"(
  manager_config:
    thread_pool:
      thread_count: 1
)";

TEST_F(FileSystemBufferFilterTest, PassesRequestHeadersThroughOnNoBody) {
  createFilterFromYaml(minimal_config); // Default filter config.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
}

TEST_F(FileSystemBufferFilterTest, PassesResponseHeadersThroughOnNoBody) {
  createFilterFromYaml(minimal_config); // Default filter config.
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
}

TEST_F(FileSystemBufferFilterTest, FullyBypassingAllowsUnspecifiedManager) {
  createFilterFromYaml(R"(
    request:
      behavior:
        bypass: {}
    response:
      behavior:
        bypass: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, true));
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, true));
}

TEST_F(FileSystemBufferFilterTest, BypassesRequestFilter) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      behavior:
        bypass: {}
  )");
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->decodeData(data2, true));
}

TEST_F(FileSystemBufferFilterTest, BypassesResponseFilter) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      behavior:
        bypass: {}
  )");
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::Continue, filter_->encodeData(data2, true));
}

TEST_F(FileSystemBufferFilterTest, BuffersEntireRequestAndReplacesContentLength) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      behavior:
        fully_buffer_and_always_inject_content_length: {}
  )");
  request_headers_.setContentLength(6);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, false));
  EXPECT_EQ(request_sent_on_, "");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data2, true));
  EXPECT_EQ(request_headers_.getContentLengthValue(), "12");
  EXPECT_EQ(request_sent_on_, "hello banana");
}

TEST_F(FileSystemBufferFilterTest, BuffersEntireResponseAndReplacesContentLength) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      behavior:
        fully_buffer_and_always_inject_content_length: {}
  )");
  response_headers_.setContentLength(6);
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(response_sent_on_, "");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, true));
  EXPECT_EQ(response_headers_.getContentLengthValue(), "12");
  EXPECT_EQ(response_sent_on_, "hello banana");
}

TEST_F(FileSystemBufferFilterTest, DoesNotBufferRequestWhenContentLengthPresent) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      behavior:
        inject_content_length_if_necessary: {}
  )");
  request_headers_.setContentLength(6);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, false));
  EXPECT_EQ(request_sent_on_, "hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data2, true));
  EXPECT_EQ(request_headers_.getContentLengthValue(), "6");
  EXPECT_EQ(request_sent_on_, "hello banana");
}

TEST_F(FileSystemBufferFilterTest, DoesNotBufferResponseWhenContentLengthPresent) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      behavior:
        inject_content_length_if_necessary: {}
  )");
  response_headers_.setContentLength(6);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(response_sent_on_, "hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, true));
  EXPECT_EQ(response_headers_.getContentLengthValue(), "6");
  EXPECT_EQ(response_sent_on_, "hello banana");
}

// When we patch in a change that makes watermark functions return bool, this will change the test
// behavior to match, so that change won't break the test.
// TODO(ravenblack): remove the branch once returning bool is stable.
template <typename R> constexpr bool watermarkReturnsVoid(R (FileSystemBufferFilter::*)()) {
  return std::is_void<R>::value;
}
constexpr int outerWatermarkAfterIntercepting() {
  return watermarkReturnsVoid(&FileSystemBufferFilter::onAboveWriteBufferHighWatermark) ? 1 : 0;
}

TEST_F(FileSystemBufferFilterTest, BuffersResponseWhenDownstreamIsSlow) {
  createFilterFromYaml(minimal_config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl data1("hello");
  Buffer::OwnedImpl data2(" banana");
  sendResponseHighWatermark();
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(response_sent_on_, "");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, true));
  EXPECT_EQ(response_sent_on_, "");
  sendResponseLowWatermark();
  EXPECT_EQ(0, response_source_watermark_);
  EXPECT_EQ(response_sent_on_, "hello banana");
  EXPECT_TRUE(continued_encoding_);
  // Ensure that any updates after continueEncoding don't cause a problem; this triggers an
  // onStateChange call, the most likely thing to be queued if the filter isn't destroyed promptly.
  sendResponseHighWatermark();
  sendResponseLowWatermark();
}

TEST_F(FileSystemBufferFilterTest,
       BuffersResponseToDiskWhenDownstreamIsSlowAndMemoryLimitIsExceeded) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      memory_buffer_bytes_limit: 6
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl data1("hello ");
  Buffer::OwnedImpl data2("wor");
  Buffer::OwnedImpl data3("ld");
  sendResponseHighWatermark();
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(response_sent_on_, "");
  expectAsyncFileCreated();
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));
  auto handle = completeCreateFileAndExpectWrite("wor");
  expectWriteWithPosition(handle, "ld", 3);
  completeWriteOfSize(3); // "wor"
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, true));
  completeWriteOfSize(2); // "ld"
  EXPECT_EQ(response_sent_on_, "");
  expectRead(handle, 0, 3); // "wor"
  sendResponseLowWatermark();
  EXPECT_EQ(response_sent_on_, "hello ");
  expectRead(handle, 3, 2); // "ld"
  completeRead("wor");
  EXPECT_EQ(response_sent_on_, "hello wor");
  completeRead("ld");
  EXPECT_EQ(response_sent_on_, "hello world");
}

TEST_F(FileSystemBufferFilterTest, RequestErrorsWhenManagerNotConfigured) {
  createFilterFromYaml(R"(
    request:
      memory_buffer_bytes_limit: 3
  )");
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer filter error", _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
}

TEST_F(FileSystemBufferFilterTest, ResponseErrorsWhenManagerNotConfigured) {
  createFilterFromYaml(R"(
    response:
      memory_buffer_bytes_limit: 3
  )");
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer filter error", _, _, _));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
}

TEST_F(FileSystemBufferFilterTest, RequestErrorsWhenTotalBufferSizeExceeded) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 3
      storage_buffer_bytes_limit: 3
      behavior:
        inject_content_length_if_necessary: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl data1("hello banana");
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::PayloadTooLarge, "buffer limit exceeded", _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(data1, true));
}

TEST_F(FileSystemBufferFilterTest, ResponseErrorsWhenTotalBufferSizeExceeded) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      memory_buffer_bytes_limit: 3
      storage_buffer_bytes_limit: 3
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl data1("hello banana");
  EXPECT_CALL(encoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer limit exceeded", _, _, _));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
}

// It is impossible to provoke this path without Envoy supporting onAboveWriteBufferHighWatermark
// on the request stream.
// This test should be mostly a copy of the Response equivalent, if that support is added.
// TEST_F(FileSystemBufferFilterTest, RequestSlowsWhenTotalBufferSizeExceededAndStreaming) {
// }

TEST_F(FileSystemBufferFilterTest, ResponseSlowsWhenTotalBufferSizeExceededAndStreaming) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      memory_buffer_bytes_limit: 10
      storage_buffer_bytes_limit: 0
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  sendResponseHighWatermark();
  Buffer::OwnedImpl data1("hello ");
  Buffer::OwnedImpl data2("wor");
  Buffer::OwnedImpl data3("ld");
  Buffer::OwnedImpl data4("s!");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));
  EXPECT_EQ(outerWatermarkAfterIntercepting() + 1, response_source_watermark_);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data4, false));
  EXPECT_EQ(outerWatermarkAfterIntercepting() + 1, response_source_watermark_);
  EXPECT_EQ("", response_sent_on_);
  sendResponseLowWatermark();
  EXPECT_EQ(0, response_source_watermark_);
  EXPECT_EQ("hello worlds!", response_sent_on_);
}

// It is impossible to provoke this path without Envoy supporting onAboveWriteBufferHighWatermark
// on the request stream.
// This test should be mostly a copy of the Response equivalent, if that support is added.
// TEST_F(FileSystemBufferFilterTest, RequestSlowsWhenDiskWriteTakesTooLongAndWatermarkReached) {
// }

TEST_F(FileSystemBufferFilterTest, ResponseSlowsWhenDiskWriteTakesTooLongAndWatermarkReached) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      memory_buffer_bytes_limit: 10
      storage_buffer_queue_high_watermark_bytes: 10
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encodeHeaders(response_headers_, false));
  // Stream recipient says stop sending right away.
  sendResponseHighWatermark();
  Buffer::OwnedImpl data1("hello worm");
  Buffer::OwnedImpl data2("hello ");
  Buffer::OwnedImpl data3("banana team!");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data1, false));
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  expectAsyncFileCreated();
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data2, false));
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(data3, false));
  // Should have sent a watermark event now, as we have 21 bytes in memory.
  EXPECT_EQ(outerWatermarkAfterIntercepting() + 1, response_source_watermark_);
  std::function<void(absl::StatusOr<size_t>)> complete_write2;
  // Data should have been chopped into memory_buffer_bytes_limit_ sized pieces, which are written
  // in reverse order so that the one that will be usable first stays in memory longer.
  auto handle = completeCreateFileAndExpectWrite("m!");
  expectWriteWithPosition(handle, "banana tea", 2);
  completeWriteOfSize(2); // "m!"
  EXPECT_EQ(outerWatermarkAfterIntercepting() + 1, response_source_watermark_);
  expectWriteWithPosition(handle, "hello ", 12);
  completeWriteOfSize(10); // "banana tea"
  completeWriteOfSize(6);  // "hello "
  // Should have sent a low watermark event on "hello " being written - now below high watermark
  // threshold.
  EXPECT_EQ(outerWatermarkAfterIntercepting(), response_source_watermark_);
  // Just destroying the filter before reading back, since that's not part of this test case.
}

TEST_F(FileSystemBufferFilterTest, BufferingToDiskBothWaysWorksAsExpected) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
    response:
      memory_buffer_bytes_limit: 20
      behavior:
        fully_buffer: {}
  )");
  Buffer::OwnedImpl request_body("abcdefghijklmnopqrstuvwxyz1234");
  Buffer::OwnedImpl response_body("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  auto request_handle = completeCreateFileAndExpectWrite("uvwxyz1234");
  expectWriteWithPosition(request_handle, "klmnopqrst", 10);
  completeWriteOfSize(10);
  completeWriteOfSize(10);
  expectRead(request_handle, 10, 10);
  Buffer::OwnedImpl empty_buffer("");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_buffer, true));
  expectRead(request_handle, 0, 10);
  completeRead("klmnopqrst");
  completeRead("uvwxyz1234");
  EXPECT_EQ("abcdefghijklmnopqrstuvwxyz1234", request_sent_on_);
  // Response writes only one chunk to storage because it had a larger memory buffer.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  expectAsyncFileCreated();
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(response_body, false));
  auto response_handle = completeCreateFileAndExpectWrite("UVWXYZ1234");
  completeWriteOfSize(10);
  expectRead(response_handle, 0, 10);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->encodeData(empty_buffer, true));
  completeRead("UVWXYZ1234");
  EXPECT_EQ("ABCDEFGHIJKLMNOPQRSTUVWXYZ1234", response_sent_on_);
}

TEST_F(FileSystemBufferFilterTest, RequestTrailersArePostponedUntilStreamComplete) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  Buffer::OwnedImpl request_body("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  EXPECT_EQ("", request_sent_on_);
  EXPECT_FALSE(continued_decoding_);
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->decodeTrailers(request_trailers_));
  EXPECT_EQ("hello", request_sent_on_);
  EXPECT_TRUE(continued_decoding_);
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->decodeTrailers(request_trailers_));
}

TEST_F(FileSystemBufferFilterTest, ResponseTrailersArePostponedUntilStreamComplete) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    response:
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
  Buffer::OwnedImpl response_body("hello");
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->encodeData(response_body, false));
  EXPECT_EQ("", response_sent_on_);
  EXPECT_FALSE(continued_encoding_);
  EXPECT_EQ(Http::FilterTrailersStatus::StopIteration, filter_->encodeTrailers(response_trailers_));
  EXPECT_EQ("hello", response_sent_on_);
  EXPECT_TRUE(continued_encoding_);
  EXPECT_EQ(Http::FilterTrailersStatus::Continue, filter_->encodeTrailers(response_trailers_));
}

TEST_F(FileSystemBufferFilterTest, FailedFileOpenOutputsError) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer filter error", _, _, _));
  // Fail the queued createAnonymousFile.
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{
      absl::UnknownError("mock file intentionally failed to create")});
  // Ensure that any updates after sendLocalReply don't cause a problem; this triggers an
  // onStateChange call, the most likely thing to be queued if the filter isn't destroyed promptly.
  sendResponseHighWatermark();
  sendResponseLowWatermark();
}

TEST_F(FileSystemBufferFilterTest, FailedFileWriteOutputsError) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  auto handle = completeCreateFileAndExpectWrite("1234567890");
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer filter error", _, _, _));
  // Fail the queued write.
  mock_async_file_manager_->nextActionCompletes(
      absl::StatusOr<size_t>(absl::UnknownError("mock file intentionally failed to write")));
}

TEST_F(FileSystemBufferFilterTest, FailedFileReadOutputsError) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  auto handle = completeCreateFileAndExpectWrite("1234567890");
  completeWriteOfSize(10);
  Buffer::OwnedImpl empty_buffer{""};
  expectRead(handle, 0, 10);
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_buffer, true));
  EXPECT_CALL(decoder_callbacks_,
              sendLocalReply(Http::Code::InternalServerError, "buffer filter error", _, _, _));
  // Fail the queued read.
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<std::unique_ptr<Buffer::Instance>>(
      absl::UnknownError("mock file intentionally failed to read")));
}

TEST_F(FileSystemBufferFilterTest, FilterDestroyedWhileFileActionIsInFlightIsOkay) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  EXPECT_CALL(*mock_async_file_manager_, mockCancel());
  destroyFilter();
}

TEST_F(FileSystemBufferFilterTest, BufferConsumedWhileFileOpeningPreventsWrite) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  // File creation should have been requested as buffer is over limit.
  Buffer::OwnedImpl empty_buffer{""};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer, filter_->decodeData(empty_buffer, true));
  // Buffer should now be emptied, file creation is still in flight.

  // Complete the file-opening without expecting a write.
  auto handle =
      std::make_shared<testing::StrictMock<MockAsyncFileContext>>(mock_async_file_manager_);
  mock_async_file_manager_->nextActionCompletes(absl::StatusOr<AsyncFileHandle>{handle});
  // There should be no action on the file handle other than the default expected close().
}

TEST_F(FileSystemBufferFilterTest, FilterDestroyedWhileFileActionIsInDispatcherIsOkay) {
  createFilterFromYaml(R"(
    manager_config:
      thread_pool:
        thread_count: 1
    request:
      memory_buffer_bytes_limit: 10
      behavior:
        fully_buffer: {}
  )");
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  expectAsyncFileCreated();
  Buffer::OwnedImpl request_body{"12345678901234567890"};
  EXPECT_EQ(Http::FilterDataStatus::StopIterationNoBuffer,
            filter_->decodeData(request_body, false));
  completeCreateFileAndExpectWrite("1234567890");
  std::function<void()> intercepted_dispatcher_callback;
  // Our default mock dispatcher behavior calls the callback immediately - here we intercept
  // one so we can call it after a 'realistic' delay during which something else happened to
  // destroy the filter.
  EXPECT_CALL(decoder_callbacks_.dispatcher_, post(_))
      .WillOnce([&intercepted_dispatcher_callback](std::function<void()> fn) {
        intercepted_dispatcher_callback = fn;
      });
  completeWriteOfSize(10);
  destroyFilter();
  // Callback called from dispatcher after filter was destroyed and its pointer invalidated,
  // should not cause a crash.
  intercepted_dispatcher_callback();
}

TEST_F(FileSystemBufferFilterTest, MergesRouteConfig) {
  createFilterFromYaml(minimal_config);
  auto vhost_config = configFromYaml(R"(
    request:
      behavior:
        fully_buffer: {}
  )");
  auto route_config = configFromYaml(R"(
    response:
      behavior:
        fully_buffer: {}
  )");
  auto mock_route = std::make_shared<Router::MockRoute>();
  EXPECT_CALL(decoder_callbacks_, route()).WillOnce(Return(mock_route));
  EXPECT_CALL(*mock_route, traversePerFilterConfig(_, _))
      .WillOnce([vhost_config, route_config](
                    const std::string&,
                    std::function<void(const Router::RouteSpecificFilterConfig&)> add_config) {
        add_config(*vhost_config);
        add_config(*route_config);
      });
  // The default config would return Continue, so these returning StopIteration shows that
  // both route_configs were applied.
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->decodeHeaders(request_headers_, false));
  EXPECT_EQ(Http::FilterHeadersStatus::StopIteration,
            filter_->encodeHeaders(response_headers_, false));
}

TEST_F(FileSystemBufferFilterTest, PassesThrough1xxHeaders) {
  createFilterFromYaml(minimal_config);
  EXPECT_EQ(Http::FilterHeadersStatus::Continue, filter_->encode1xxHeaders(response_headers_));
}

TEST_F(FileSystemBufferFilterTest, PassesThroughMetadata) {
  createFilterFromYaml(minimal_config);
  Http::MetadataMap metadata;
  EXPECT_EQ(Http::FilterMetadataStatus::Continue, filter_->encodeMetadata(metadata));
}

} // namespace FileSystemBuffer
} // namespace HttpFilters
} // namespace Extensions
} // namespace Envoy
